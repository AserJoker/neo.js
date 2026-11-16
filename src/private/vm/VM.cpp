// VM.cpp - V0 执行循环（ADR-060）
//
// 主循环只取 opcode、查分派表、消费处理函数返回的信号；pc 由处理函数推进。
// 所有 JS 语义（转换/运算/绑定规则）都交给 Engine 原语，VM 不重复编排。
//
// 关联 ADR: 004（执行模型）、006（异常即值）、016（运算符原语）、
//          024（字节码指令集）、037（调用栈）、060（VM 框架）

#include "vm/VM.h"

#include "core/ErrorKind.h"
#include "core/IContext.h"
#include "core/InterruptKind.h"
#include "core/OperatorKind.h"
#include "core/Scope.h"
#include "core/types/Exception.h"
#include "core/types/Interrupt.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <utility>

namespace neo::vm {

namespace detail = neo::core::detail;

using neo::core::BinaryOperator;
using neo::core::ErrorKind;
using neo::core::IContext;
using neo::core::Scope;
using neo::core::SourcePosition;
using neo::core::Value;

namespace {

// 把类数组的 length 个元素读入 out（展开调用实参用）。
// 成功返回 nullptr，失败返回错误值。
Value* readArrayElements(IContext* ctx, Value* array,
                         std::vector<Value*>& out) {
    Value* lengthValue = ctx->getProperty(array, "length");
    if (detail::isErrorValue(lengthValue)) return lengthValue;
    Value* lengthNumber = ctx->toNumber(lengthValue);
    if (detail::isErrorValue(lengthNumber)) return lengthNumber;
    double length = static_cast<neo::core::NumberData*>(lengthNumber->data())->value;
    if (length <= 0.0) return nullptr;
    const size_t count = static_cast<size_t>(length);
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Value* value =
            ctx->getProperty(array, ctx->createString(std::to_string(i)));
        if (detail::isErrorValue(value)) return value;
        out.push_back(value);
    }
    return nullptr;
}

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

const char* opCodeName(OpCode op) {
    switch (op) {
        case OpCode::PushUndefined: return "push_undefined";
        case OpCode::PushUninitialized: return "push_uninitialized";
        case OpCode::PushNull:      return "push_null";
        case OpCode::PushTrue:      return "push_true";
        case OpCode::PushFalse:     return "push_false";
        case OpCode::PushNumber:    return "push_number";
        case OpCode::PushString:    return "push_string";
        case OpCode::Pop:           return "pop";
        case OpCode::Dup:           return "dup";
        case OpCode::Clone:         return "clone";
        case OpCode::Swap:          return "swap";
        case OpCode::Define:        return "define";
        case OpCode::Initialize:    return "initialize";
        case OpCode::Load:          return "load";
        case OpCode::Store:         return "store";
        case OpCode::GetProp:       return "get_prop";
        case OpCode::SetProp:       return "set_prop";
        case OpCode::NewObject:     return "new_object";
        case OpCode::NewArray:      return "new_array";
        case OpCode::DefineProp:    return "define_prop";
        case OpCode::TemplateObject: return "template_object";
        case OpCode::ArrayAppend:   return "array_append";
        case OpCode::ArrayGrow:     return "array_grow";
        case OpCode::ArraySpread:   return "array_spread";
        case OpCode::ObjectSpread:  return "object_spread";
        case OpCode::ObjectRest:    return "object_rest";
        case OpCode::ForInKeys:     return "for_in_keys";
        case OpCode::IsEnumerableOwn: return "is_enumerable_own";
        case OpCode::GetIterator:   return "get_iterator";
        case OpCode::IteratorNext:  return "iterator_next";
        case OpCode::IteratorClose: return "iterator_close";
        case OpCode::Add:           return "add";
        case OpCode::Sub:           return "sub";
        case OpCode::Mul:           return "mul";
        case OpCode::Div:           return "div";
        case OpCode::Lt:            return "lt";
        case OpCode::Gt:            return "gt";
        case OpCode::Le:            return "le";
        case OpCode::Ge:            return "ge";
        case OpCode::StrictEqual:   return "strict_equal";
        case OpCode::Unary:         return "unary";
        case OpCode::ToString:      return "to_string";
        case OpCode::Jmp:           return "jmp";
        case OpCode::JTrue:         return "jtrue";
        case OpCode::JFalse:        return "jfalse";
        case OpCode::JNotNullish:   return "jnotnullish";
        case OpCode::TryBegin:      return "try_begin";
        case OpCode::TryBodyEnd:    return "try_body_end";
        case OpCode::CatchEnd:      return "catch_end";
        case OpCode::FinallyEnd:    return "finally_end";
        case OpCode::Throw:         return "throw";
        case OpCode::PushLabel:     return "push_label";
        case OpCode::PopLabel:      return "pop_label";
        case OpCode::PopLabelIfTop: return "pop_label_if_top";
        case OpCode::Break:         return "break";
        case OpCode::Continue:      return "continue";
        case OpCode::PushScope:     return "push_scope";
        case OpCode::PopScope:      return "pop_scope";
        case OpCode::IterationEnv:  return "iteration_env";
        case OpCode::CommitIterationEnv: return "commit_iteration_env";
        case OpCode::PushFunction:  return "push_function";
        case OpCode::SetClosure:    return "set_closure";
        case OpCode::SetFuncName:   return "set_func_name";
        case OpCode::BindThis:      return "bind_this";
        case OpCode::LoadThis:      return "load_this";
        case OpCode::StoreThis:     return "store_this";
        case OpCode::Call:          return "call";
        case OpCode::CallMethod:    return "call_method";
        case OpCode::CallSpread:    return "call_spread";
        case OpCode::CallMethodSpread: return "call_method_spread";
        case OpCode::New:           return "new";
        case OpCode::NewSpread:     return "new_spread";
        case OpCode::LoadNewTarget: return "load_new_target";
        case OpCode::Yield:         return "yield";
        case OpCode::Await:         return "await";
        case OpCode::Ret:           return "ret";
        case OpCode::Halt:          return "hlt";
        case OpCode::RestArgs:      return "rest_args";
        case OpCode::PushClass:     return "push_class";
        case OpCode::SetStrict:     return "set_strict";
        case OpCode::InitInstanceElements: return "init_instance_elements";
        case OpCode::ConstructSuper: return "construct_super";
        case OpCode::ConstructSuperSpread: return "construct_super_spread";
        case OpCode::GetSuperProp:  return "get_super_prop";
        case OpCode::SetSuperProp:  return "set_super_prop";
        case OpCode::DefineClassMethod: return "define_class_method";
        case OpCode::DefineClassField: return "define_class_field";
        case OpCode::YieldDelegate: return "yield_delegate";
        case OpCode::Dup2:          return "dup2";
        case OpCode::DeleteProp:    return "delete_prop";
        case OpCode::SetPropKeep:   return "set_prop_keep";
        case OpCode::SetSuperPropKeep: return "set_super_prop_keep";
    }
    return "<unknown>";
}

VM::VM(IContext* ctx, const Code* code)
    : ctx_(ctx),
      code_(code),
      entryScope_(ctx->current()),
      rootScope_(ctx->root()) {}

const VM::Handler* VM::handlers() {
    static const Handler table[kOpCodeCount] = {
        &VM::opPushUndefined,
        &VM::opPushUninitialized,
        &VM::opPushNull,
        &VM::opPushTrue,
        &VM::opPushFalse,
        &VM::opPushNumber,
        &VM::opPushString,
        &VM::opPop,
        &VM::opDup,
        &VM::opClone,
        &VM::opSwap,
        &VM::opDefine,
        &VM::opInitialize,
        &VM::opLoad,
        &VM::opStore,
        &VM::opGetProp,
        &VM::opSetProp,
        &VM::opNewObject,
        &VM::opNewArray,
        &VM::opDefineProp,
        &VM::opTemplateObject,
        &VM::opArrayAppend,
        &VM::opArrayGrow,
        &VM::opArraySpread,
        &VM::opObjectSpread,
        &VM::opObjectRest,
        &VM::opForInKeys,
        &VM::opIsEnumerableOwn,
        &VM::opGetIterator,
        &VM::opIteratorNext,
        &VM::opIteratorClose,
        &VM::opAdd,
        &VM::opSub,
        &VM::opMul,
        &VM::opDiv,
        &VM::opLt,
        &VM::opGt,
        &VM::opLe,
        &VM::opGe,
        &VM::opStrictEqual,
        &VM::opUnary,
        &VM::opToString,
        &VM::opJmp,
        &VM::opJTrue,
        &VM::opJFalse,
        &VM::opJNotNullish,
        &VM::opTryBegin,
        &VM::opTryBodyEnd,
        &VM::opCatchEnd,
        &VM::opFinallyEnd,
        &VM::opThrow,
        &VM::opPushLabel,
        &VM::opPopLabel,
        &VM::opPopLabelIfTop,
        &VM::opBreak,
        &VM::opContinue,
        &VM::opPushScope,
        &VM::opPopScope,
        &VM::opIterationEnv,
        &VM::opCommitIterationEnv,
        &VM::opPushFunction,
        &VM::opSetClosure,
        &VM::opSetFuncName,
        &VM::opBindThis,
        &VM::opLoadThis,
        &VM::opStoreThis,
        &VM::opCall,
        &VM::opCallMethod,
        &VM::opCallSpread,
        &VM::opCallMethodSpread,
        &VM::opNew,
        &VM::opNewSpread,
        &VM::opLoadNewTarget,
        &VM::opYield,
        &VM::opAwait,
        &VM::opRet,
        &VM::opHalt,
        &VM::opRestArgs,
        &VM::opPushClass,
        &VM::opSetStrict,
        &VM::opInitInstanceElements,
        &VM::opConstructSuper,
        &VM::opConstructSuperSpread,
        &VM::opGetSuperProp,
        &VM::opSetSuperProp,
        &VM::opDefineClassMethod,
        &VM::opDefineClassField,
        &VM::opYieldDelegate,
        &VM::opDup2,
        &VM::opDeleteProp,
        &VM::opSetPropKeep,
        &VM::opSetSuperPropKeep,
    };
    return table;
}

SourcePosition VM::currentPosition() const {
    const SourcePosition* position = code_->sourceMap.lookup(instructionPc_);
    return position == nullptr ? SourcePosition{} : *position;
}

const std::string* VM::stringOperand(uint32_t index) const {
    if (index >= code_->strings.size()) return nullptr;
    return &code_->strings[index];
}

Value* VM::fail(Value* error) {
    // Engine 原语创建错误时不知道 pc；VM 用 SourceMap 补全异常栈顶位置。
    // 异常展开由 run 主循环统一处理（Fatal 不参与）。
    ctx_->setExceptionPosition(error, currentPosition());
    return error;
}

Value* VM::badBytecode(const char* message) {
    return fail(ctx_->createErrorException(ErrorKind::Error, message));
}

void VM::restoreFrame(const TryFrame& frame) {
    if (stack_.size() > frame.stackDepth) stack_.resize(frame.stackDepth);
    if (labelStack_.size() > frame.labelDepth) {
        labelStack_.resize(frame.labelDepth);
    }
    while (ctx_->current() != nullptr && ctx_->current() != frame.scope) {
        ctx_->popScope();
    }
}

void VM::trackRes(TryFrame& frame, Value* res) {
    if (res != nullptr && frame.scope != nullptr) frame.scope->add(res);
}

Value* VM::unwindReturn(Value* value) {
    while (!tryStack_.empty()) {
        TryFrame& frame = tryStack_.back();
        if (frame.finallyStart != 0) {
            uint32_t finallyPc = frame.finallyStart;
            frame.finallyStart = 0;
            frame.res = value;
            trackRes(frame, value);
            restoreFrame(frame);
            pc_ = finallyPc;
            return nullptr;
        }
        restoreFrame(frame);
        tryStack_.pop_back();
    }
    return value;
}

Value* VM::dispatchException(Value* exception) {
    while (!tryStack_.empty()) {
        TryFrame& frame = tryStack_.back();
        if (frame.catchStart != 0) {
            uint32_t catchPc = frame.catchStart;
            frame.catchStart = 0;
            // restore 可能 pop 作用域并触发 GC，先把异常挂到入口作用域。
            trackRes(frame, exception);
            restoreFrame(frame);

            Value* child =
                static_cast<neo::core::ExceptionData*>(exception->data())->child;
            // catch 作用域由编译器创建；在此之前先挂在入口作用域保活。
            trackRes(frame, child);
            push(child);
            pc_ = catchPc;
            return nullptr;
        }
        if (frame.finallyStart != 0) {
            uint32_t finallyPc = frame.finallyStart;
            frame.finallyStart = 0;
            frame.res = exception;
            trackRes(frame, exception);
            restoreFrame(frame);
            pc_ = finallyPc;
            return nullptr;
        }
        restoreFrame(frame);
        tryStack_.pop_back();
    }
    return exception;
}

Value* VM::dispatchPending(Value* res) {
    if (res == nullptr) return nullptr;
    if (res->type()->kind() == neo::core::TypeKind::Exception) {
        return dispatchException(res);
    }
    if (detail::isInterruptValue(res)) {
        auto* data = neo::core::asInterrupt(res);
        if (data != nullptr &&
            (data->kind == neo::core::InterruptKind::Break ||
             data->kind == neo::core::InterruptKind::Continue)) {
            return handleLabelInterrupt(res);
        }
    }
    return unwindReturn(res);
}

Value* VM::handleTryBodyEnd() {
    if (tryStack_.empty()) return badBytecode("try_body_end: empty try stack");
    TryFrame& frame = tryStack_.back();
    if (frame.finallyStart != 0) {
        uint32_t finallyPc = frame.finallyStart;
        frame.finallyStart = 0;
        restoreFrame(frame);
        pc_ = finallyPc;
        return nullptr;
    }
    uint32_t endPc = frame.tryEnd;
    restoreFrame(frame);
    tryStack_.pop_back();
    pc_ = endPc;
    return nullptr;
}

Value* VM::handleCatchEnd() {
    if (tryStack_.empty()) return badBytecode("catch_end: empty try stack");
    TryFrame& frame = tryStack_.back();
    if (frame.finallyStart != 0) {
        uint32_t finallyPc = frame.finallyStart;
        frame.finallyStart = 0;
        restoreFrame(frame);
        pc_ = finallyPc;
        return nullptr;
    }
    uint32_t endPc = frame.tryEnd;
    restoreFrame(frame);
    tryStack_.pop_back();
    pc_ = endPc;
    return nullptr;
}

Value* VM::handleFinallyEnd() {
    if (tryStack_.empty()) return badBytecode("finally_end: empty try stack");
    TryFrame& frame = tryStack_.back();
    Value* res = frame.res;
    uint32_t endPc = frame.tryEnd;
    restoreFrame(frame);
    tryStack_.pop_back();

    if (res == nullptr) {
        pc_ = endPc;
        return nullptr;
    }
    // 帧已弹出：先把 pending 值挂到当前作用域，跨帧展开期间保活。
    if (ctx_->current() != nullptr) ctx_->current()->add(res);
    return dispatchPending(res);
}

VmState VM::snapshot() const {
    VmState state;
    state.pc = pc_;
    state.stack = stack_;
    state.labelStack = labelStack_;
    state.tryStack = tryStack_;
    state.entryScope = entryScope_;
    state.rootScope = rootScope_;
    state.currentScope = ctx_->current();
    state.delegateIterator = delegateIterator_;
    return state;
}

void VM::restore(const VmState& state) {
    pc_ = state.pc;
    instructionPc_ = state.pc;
    stack_ = state.stack;
    labelStack_ = state.labelStack;
    tryStack_ = state.tryStack;
    entryScope_ = state.entryScope;
    rootScope_ = state.rootScope;
    delegateIterator_ = state.delegateIterator;
    if (state.currentScope != nullptr) {
        ctx_->setCurrentScope(state.currentScope);
    }
}

Value* VM::resume(const VmState& state, Value* sent) {
    restore(state);
    // yield* 挂起点：把注入值转发给被委托迭代器，而不是当作普通 yield 结果。
    if (delegateIterator_ != nullptr) {
        Value* pending = delegateStep(sent);
        if (pending != nullptr) return pending;
        return execute();
    }
    if (sent != nullptr && detail::isInterruptValue(sent)) {
        auto* data = neo::core::asInterrupt(sent);
        if (data != nullptr) {
            Value* pending = nullptr;
            if (data->kind == neo::core::InterruptKind::Throw) {
                pending = dispatchException(data->value);
            } else if (data->kind == neo::core::InterruptKind::Return) {
                pending = unwindReturn(data->value);
            }
            if (pending != nullptr) return pending;
        }
    } else {
        push(sent != nullptr ? sent : ctx_->createUndefined());
    }
    return execute();
}

Value* VM::run(uint32_t entryPc) {
    pc_ = entryPc;
    instructionPc_ = entryPc;
    return execute();
}

Value* VM::execute() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (pc_ > bytes.size()) {
        return badBytecode("run: entry pc out of range");
    }

    const Handler* table = handlers();
    while (pc_ < bytes.size()) {
        if (bytes.size() - pc_ < 2) {
            return badBytecode("truncated opcode");
        }
        uint16_t op = readU16(bytes.data() + pc_);
        if (op >= kOpCodeCount) {
            return badBytecode("unknown opcode");
        }

        instructionPc_ = pc_;
        pc_ += 2;  // 取指：跳过 opcode，处理函数继续推进操作数
        Value* signal = (this->*table[op])();
        if (signal != nullptr) {
            // VM 内部信号：由主循环消费，不逃逸到调用方。
            if (detail::isInterruptValue(signal)) {
                auto* data = neo::core::asInterrupt(signal);
                if (data != nullptr) {
                    Value* error = nullptr;
                    switch (data->kind) {
                        case neo::core::InterruptKind::Break:
                        case neo::core::InterruptKind::Continue:
                            error = handleLabelInterrupt(signal);
                            break;
                        case neo::core::InterruptKind::TryBodyEnd:
                            error = handleTryBodyEnd();
                            break;
                        case neo::core::InterruptKind::CatchEnd:
                            error = handleCatchEnd();
                            break;
                        case neo::core::InterruptKind::FinallyEnd:
                            error = handleFinallyEnd();
                            break;
                        default:
                            return signal;  // Yield/Await 交给调用方
                    }
                    if (error != nullptr) return error;
                    continue;
                }
            }
            // Exception 进入 try 展开；Fatal 与正常返回值直接返回。
            if (signal->type()->kind() == neo::core::TypeKind::Exception) {
                Value* error = dispatchException(signal);
                if (error != nullptr) return error;
                continue;
            }
            return signal;
        }
    }
    // 指令流自然结束：等价于 hlt。
    return ctx_->createUndefined();
}

// —— 字面量 ——

Value* VM::opPushUndefined() {
    push(ctx_->createUndefined());
    return nullptr;
}

Value* VM::opPushUninitialized() {
    push(ctx_->createUninitialized());
    return nullptr;
}

Value* VM::opPushNull() {
    push(ctx_->createNull());
    return nullptr;
}

Value* VM::opPushTrue() {
    push(ctx_->createBoolean(true));
    return nullptr;
}

Value* VM::opPushFalse() {
    push(ctx_->createBoolean(false));
    return nullptr;
}

Value* VM::opPushNumber() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("push_number: truncated operand");
    }
    uint32_t index = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (index >= code_->numbers.size()) {
        return badBytecode("push_number: constant index out of range");
    }
    push(ctx_->createNumber(code_->numbers[index]));
    return nullptr;
}

Value* VM::opPushString() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("push_string: truncated operand");
    }
    uint32_t index = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* value = stringOperand(index);
    if (value == nullptr) {
        return badBytecode("push_string: constant index out of range");
    }
    push(ctx_->createString(*value));
    return nullptr;
}

// —— 栈操作 ——

Value* VM::opPop() {
    if (stack_.empty()) return badBytecode("pop: operand stack underflow");
    pop();
    return nullptr;
}

Value* VM::opDup() {
    if (stack_.empty()) return badBytecode("dup: operand stack underflow");
    push(top());
    return nullptr;
}

Value* VM::opClone() {
    if (stack_.empty()) return badBytecode("clone: operand stack underflow");
    Value* copy = ctx_->cloneValue(top());
    if (copy == ctx_->oomValue()) return copy;
    push(copy);
    return nullptr;
}

Value* VM::opSwap() {
    if (stack_.size() < 2) return badBytecode("swap: operand stack underflow");
    std::swap(stack_[stack_.size() - 1], stack_[stack_.size() - 2]);
    return nullptr;
}

Value* VM::opDup2() {
    if (stack_.size() < 2) return badBytecode("dup2: operand stack underflow");
    Value* a = stack_[stack_.size() - 2];
    Value* b = stack_[stack_.size() - 1];
    stack_.push_back(a);
    stack_.push_back(b);
    return nullptr;
}

Value* VM::opDeleteProp() {
    if (stack_.size() < 2) {
        return badBytecode("delete_prop: operand stack underflow");
    }
    Value* key = pop();
    Value* object = pop();
    Value* result = ctx_->deleteProperty(object, key);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opSetPropKeep() {
    if (stack_.size() < 4) {
        return badBytecode("set_prop_keep: operand stack underflow");
    }
    Value* keep = pop();
    Value* value = pop();
    Value* key = pop();
    Value* object = pop();
    Value* result = ctx_->setProperty(object, key, value);
    if (detail::isErrorValue(result)) return fail(result);
    push(keep);
    return nullptr;
}

Value* VM::opSetSuperPropKeep() {
    if (stack_.size() < 3) {
        return badBytecode("set_super_prop_keep: operand stack underflow");
    }
    Value* keep = pop();
    Value* value = pop();
    Value* key = pop();
    Value* homeObject = ctx_->functionHomeObject(ctx_->currentFunction());
    Value* receiver = ctx_->getThis();
    if (detail::isErrorValue(receiver)) return fail(receiver);
    Value* result =
        ctx_->setSuperProperty(homeObject, key, value, receiver);
    if (detail::isErrorValue(result)) return fail(result);
    push(keep);
    return nullptr;
}

// —— 绑定 ——

Value* VM::opDefine() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 8) {
        return badBytecode("define: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    uint32_t flags = readU32(bytes.data() + pc_ + 4);
    pc_ += 8;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) return badBytecode("define: name index out of range");
    if (stack_.empty()) return badBytecode("define: operand stack underflow");

    Value* result = ctx_->define(*name, pop(),
                                 (flags & kDefineMutable) != 0,
                                 (flags & kDefineInitialized) != 0);
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opInitialize() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("initialize: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) return badBytecode("initialize: name index out of range");
    if (stack_.empty()) return badBytecode("initialize: operand stack underflow");

    Value* result = ctx_->initialize(*name, pop());
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opLoad() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("load: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) return badBytecode("load: name index out of range");

    Value* result = ctx_->load(*name);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opStore() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("store: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) return badBytecode("store: name index out of range");
    if (stack_.empty()) return badBytecode("store: operand stack underflow");

    Value* result = ctx_->store(*name, pop());
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

// —— 属性 ——

Value* VM::opGetProp() {
    if (stack_.size() < 2) {
        return badBytecode("get_prop: operand stack underflow");
    }
    Value* key = pop();
    Value* object = pop();
    Value* result = ctx_->getProperty(object, key);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opSetProp() {
    if (stack_.size() < 3) {
        return badBytecode("set_prop: operand stack underflow");
    }
    Value* value = pop();
    Value* key = pop();
    Value* object = pop();
    Value* result = ctx_->setProperty(object, key, value);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opNewObject() {
    Value* object = ctx_->createObject();
    if (object == ctx_->oomValue()) return object;
    push(object);
    return nullptr;
}

Value* VM::opNewArray() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("new_array: truncated operand");
    }
    uint32_t length = readU32(bytes.data() + pc_);
    pc_ += 4;

    Value* array = ctx_->createArray();
    if (array == ctx_->oomValue()) return array;
    if (length != 0) {
        Value* result = ctx_->defineProperty(
            array, "length", ctx_->createNumber(static_cast<double>(length)),
            true, false, false);
        if (detail::isErrorValue(result)) return fail(result);
    }
    push(array);
    return nullptr;
}

Value* VM::opDefineProp() {
    if (stack_.size() < 3) {
        return badBytecode("define_prop: operand stack underflow");
    }
    Value* value = pop();
    Value* key = pop();
    Value* object = pop();
    Value* result = ctx_->defineProperty(object, key, value);
    if (detail::isErrorValue(result)) return fail(result);
    push(object);
    return nullptr;
}

Value* VM::opTemplateObject() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("template_object: truncated operand");
    }
    uint32_t count = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (bytes.size() - pc_ < static_cast<size_t>(count) * 8) {
        return badBytecode("template_object: truncated quasis");
    }
    std::vector<std::pair<std::optional<std::string>, std::string>> quasis;
    quasis.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t cookedIndex = readU32(bytes.data() + pc_);
        pc_ += 4;
        uint32_t rawIndex = readU32(bytes.data() + pc_);
        pc_ += 4;
        const std::string* raw = stringOperand(rawIndex);
        if (raw == nullptr) {
            return badBytecode("template_object: string index out of range");
        }
        if (cookedIndex == Code::kUndefinedString) {
            quasis.emplace_back(std::nullopt, *raw);
            continue;
        }
        const std::string* cooked = stringOperand(cookedIndex);
        if (cooked == nullptr) {
            return badBytecode("template_object: string index out of range");
        }
        quasis.emplace_back(*cooked, *raw);
    }
    for (const auto& entry : code_->templateCache) {
        if (entry.first == instructionPc_) {
            push(entry.second);
            return nullptr;
        }
    }
    Value* object = ctx_->createTemplateObject(quasis);
    if (detail::isErrorValue(object)) return fail(object);
    code_->templateCache.emplace_back(instructionPc_, object);
    push(object);
    return nullptr;
}

Value* VM::opArrayAppend() {
    if (stack_.size() < 2) {
        return badBytecode("array_append: operand stack underflow");
    }
    Value* value = pop();
    Value* array = pop();
    Value* result = ctx_->arrayAppend(array, value);
    if (detail::isErrorValue(result)) return fail(result);
    push(array);
    return nullptr;
}

Value* VM::opArrayGrow() {
    if (stack_.empty()) {
        return badBytecode("array_grow: operand stack underflow");
    }
    Value* array = pop();
    Value* lengthValue = ctx_->getProperty(array, "length");
    if (detail::isErrorValue(lengthValue)) return fail(lengthValue);
    Value* lengthNumber = ctx_->toNumber(lengthValue);
    if (detail::isErrorValue(lengthNumber)) return fail(lengthNumber);
    double length = static_cast<neo::core::NumberData*>(lengthNumber->data())->value;
    Value* newLength = ctx_->createNumber(length + 1.0);
    Value* result = ctx_->setProperty(array, "length", newLength);
    if (detail::isErrorValue(result)) return fail(result);
    push(array);
    return nullptr;
}

Value* VM::opArraySpread() {
    if (stack_.size() < 2) {
        return badBytecode("array_spread: operand stack underflow");
    }
    Value* iterable = pop();
    Value* array = pop();
    Value* iterator = detail::getIterator(ctx_, iterable);
    if (detail::isErrorValue(iterator)) return fail(iterator);

    while (true) {
        Value* result = detail::iteratorNext(ctx_, iterator);
        if (detail::isErrorValue(result)) {
            (void)detail::iteratorClose(ctx_, iterator, nullptr);
            return fail(result);
        }
        Value* done = ctx_->getProperty(result, "done");
        if (detail::isErrorValue(done)) {
            (void)detail::iteratorClose(ctx_, iterator, nullptr);
            return fail(done);
        }
        if (detail::toBoolean(done)) break;
        Value* value = ctx_->getProperty(result, "value");
        if (detail::isErrorValue(value)) {
            (void)detail::iteratorClose(ctx_, iterator, nullptr);
            return fail(value);
        }
        Value* appended = ctx_->arrayAppend(array, value);
        if (detail::isErrorValue(appended)) {
            (void)detail::iteratorClose(ctx_, iterator, nullptr);
            return fail(appended);
        }
    }
    push(array);
    return nullptr;
}

Value* VM::opObjectSpread() {
    if (stack_.size() < 2) {
        return badBytecode("object_spread: operand stack underflow");
    }
    Value* source = pop();
    Value* target = pop();
    Value* result = ctx_->copyDataProperties(target, source);
    if (detail::isErrorValue(result)) return fail(result);
    push(target);
    return nullptr;
}

Value* VM::opObjectRest() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("object_rest: truncated operand");
    }
    uint32_t count = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (bytes.size() - pc_ < static_cast<size_t>(count) * 4) {
        return badBytecode("object_rest: truncated keys");
    }
    std::vector<Value*> excluded;
    excluded.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t index = readU32(bytes.data() + pc_);
        pc_ += 4;
        const std::string* key = stringOperand(index);
        if (key == nullptr) {
            return badBytecode("object_rest: string index out of range");
        }
        excluded.push_back(ctx_->createString(*key));
    }
    if (stack_.size() < 2) {
        return badBytecode("object_rest: operand stack underflow");
    }
    Value* source = pop();
    Value* target = pop();
    Value* result = ctx_->copyDataProperties(target, source, excluded);
    if (detail::isErrorValue(result)) return fail(result);
    push(target);
    return nullptr;
}

Value* VM::opForInKeys() {
    if (stack_.empty()) {
        return badBytecode("for_in_keys: operand stack underflow");
    }
    Value* object = pop();
    Value* keys = ctx_->forInKeys(object);
    if (detail::isErrorValue(keys)) return fail(keys);
    push(keys);
    return nullptr;
}

Value* VM::opIsEnumerableOwn() {
    if (stack_.size() < 2) {
        return badBytecode("is_enumerable_own: operand stack underflow");
    }
    Value* key = pop();
    Value* object = pop();
    Value* result = ctx_->isEnumerableOwn(object, key);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opGetIterator() {
    if (stack_.empty()) {
        return badBytecode("get_iterator: operand stack underflow");
    }
    Value* iterable = pop();
    Value* iterator = detail::getIterator(ctx_, iterable);
    if (detail::isErrorValue(iterator)) return fail(iterator);
    push(iterator);
    return nullptr;
}

Value* VM::opIteratorNext() {
    if (stack_.empty()) {
        return badBytecode("iterator_next: operand stack underflow");
    }
    Value* iterator = pop();
    Value* result = detail::iteratorNext(ctx_, iterator);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opIteratorClose() {
    if (stack_.empty()) {
        return badBytecode("iterator_close: operand stack underflow");
    }
    Value* iterator = pop();
    Value* error = detail::iteratorClose(ctx_, iterator, nullptr);
    if (error != nullptr && detail::isErrorValue(error)) {
        // 已有异常在展开时，IteratorClose 的错误不覆盖原异常（§7.4.6）。
        if (!tryStack_.empty() && tryStack_.back().res != nullptr &&
            tryStack_.back().res->type()->kind() ==
                neo::core::TypeKind::Exception) {
            return nullptr;
        }
        return fail(error);
    }
    return nullptr;
}

// —— 算术 ——

Value* VM::binary(BinaryOperator op) {
    if (stack_.size() < 2) {
        return badBytecode("binary op: operand stack underflow");
    }
    Value* right = pop();
    Value* left = pop();
    Value* result = ctx_->binaryOp(op, left, right);
    if (detail::isErrorValue(result)) return fail(result);
    // Interrupt 是控制流信号（协程/异步挂起），原样交给外部消费。
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opAdd() { return binary(BinaryOperator::Add); }
Value* VM::opSub() { return binary(BinaryOperator::Sub); }
Value* VM::opMul() { return binary(BinaryOperator::Mul); }
Value* VM::opDiv() { return binary(BinaryOperator::Div); }

Value* VM::opLt() { return binary(BinaryOperator::LessThan); }
Value* VM::opGt() { return binary(BinaryOperator::GreaterThan); }
Value* VM::opLe() { return binary(BinaryOperator::LessThanOrEqual); }
Value* VM::opGe() { return binary(BinaryOperator::GreaterThanOrEqual); }
Value* VM::opStrictEqual() { return binary(BinaryOperator::StrictEqual); }

Value* VM::opUnary() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("unary: truncated operand");
    uint32_t op = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.empty()) return badBytecode("unary: operand stack underflow");
    Value* value = pop();
    Value* result =
        ctx_->unaryOp(static_cast<neo::core::UnaryOperator>(op), value);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opToString() {
    if (stack_.empty()) return badBytecode("to_string: operand stack underflow");
    Value* value = pop();
    Value* result = ctx_->toString(value);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

// —— 控制流 ——

Value* VM::opJmp() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("jmp: truncated operand");
    uint32_t target = readU32(bytes.data() + pc_);
    if (target > bytes.size()) return badBytecode("jmp: target out of range");
    pc_ = target;
    return nullptr;
}

Value* VM::opJTrue() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("jtrue: truncated operand");
    uint32_t target = readU32(bytes.data() + pc_);
    if (target > bytes.size()) return badBytecode("jtrue: target out of range");
    pc_ += 4;
    if (stack_.empty()) return badBytecode("jtrue: operand stack underflow");
    if (ctx_->toBoolean(pop())) pc_ = target;
    return nullptr;
}

Value* VM::opJFalse() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("jfalse: truncated operand");
    uint32_t target = readU32(bytes.data() + pc_);
    if (target > bytes.size()) return badBytecode("jfalse: target out of range");
    pc_ += 4;
    if (stack_.empty()) return badBytecode("jfalse: operand stack underflow");
    if (!ctx_->toBoolean(pop())) pc_ = target;
    return nullptr;
}

Value* VM::opJNotNullish() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("jnotnullish: truncated operand");
    }
    uint32_t target = readU32(bytes.data() + pc_);
    if (target > bytes.size()) {
        return badBytecode("jnotnullish: target out of range");
    }
    pc_ += 4;
    if (stack_.empty()) {
        return badBytecode("jnotnullish: operand stack underflow");
    }
    neo::core::TypeKind kind = pop()->type()->kind();
    if (kind != neo::core::TypeKind::Null &&
        kind != neo::core::TypeKind::Undefined) {
        pc_ = target;
    }
    return nullptr;
}

// —— try ——

Value* VM::opTryBegin() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 12) {
        return badBytecode("try_begin: truncated operand");
    }
    TryFrame frame;
    frame.catchStart = readU32(bytes.data() + pc_);
    frame.finallyStart = readU32(bytes.data() + pc_ + 4);
    frame.tryEnd = readU32(bytes.data() + pc_ + 8);
    pc_ += 12;
    frame.scope = ctx_->current();
    frame.stackDepth = stack_.size();
    frame.labelDepth = labelStack_.size();
    tryStack_.push_back(frame);
    return nullptr;
}

Value* VM::opTryBodyEnd() {
    return ctx_->createInterrupt(neo::core::InterruptKind::TryBodyEnd, pc_, nullptr);
}

Value* VM::opCatchEnd() {
    return ctx_->createInterrupt(neo::core::InterruptKind::CatchEnd, pc_, nullptr);
}

Value* VM::opFinallyEnd() {
    return ctx_->createInterrupt(neo::core::InterruptKind::FinallyEnd, pc_, nullptr);
}

Value* VM::opThrow() {
    if (stack_.empty()) return badBytecode("throw: operand stack underflow");
    Value* value = pop();
    Value* exception = ctx_->createException(value);
    if (exception == ctx_->oomValue()) return exception;
    return fail(exception);
}

// —— label 栈 ——

Value* VM::opPushLabel() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 12) {
        return badBytecode("push_label: truncated operand");
    }
    uint32_t labelIndex = readU32(bytes.data() + pc_);
    uint32_t startPc = readU32(bytes.data() + pc_ + 4);
    uint32_t endPc = readU32(bytes.data() + pc_ + 8);
    pc_ += 12;

    const std::string* label = stringOperand(labelIndex);
    if (label == nullptr) {
        return badBytecode("push_label: label index out of range");
    }
    labelStack_.push_back(LabelFrame{*label, startPc, endPc, ctx_->current(),
                                     stack_.size(), tryStack_.size()});
    return nullptr;
}

Value* VM::opPopLabel() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("pop_label: truncated operand");
    }
    uint32_t labelIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* label = stringOperand(labelIndex);
    if (label == nullptr) {
        return badBytecode("pop_label: label index out of range");
    }
    if (labelStack_.empty()) {
        return badBytecode("pop_label: label stack underflow");
    }
    if (labelStack_.back().label != *label) {
        return badBytecode("pop_label: label mismatch");
    }
    labelStack_.pop_back();
    return nullptr;
}

Value* VM::opPopLabelIfTop() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("pop_label_if_top: truncated operand");
    }
    uint32_t labelIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* label = stringOperand(labelIndex);
    if (label == nullptr) {
        return badBytecode("pop_label_if_top: label index out of range");
    }
    if (!labelStack_.empty() && labelStack_.back().label == *label) {
        labelStack_.pop_back();
    }
    return nullptr;
}

Value* VM::opBreak() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("break: truncated operand");
    uint32_t labelIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    Value* interrupt = ctx_->createInterrupt(neo::core::InterruptKind::Break,
                                             labelIndex, nullptr);
    return interrupt;
}

Value* VM::opContinue() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("continue: truncated operand");
    }
    uint32_t labelIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    Value* interrupt = ctx_->createInterrupt(
        neo::core::InterruptKind::Continue, labelIndex, nullptr);
    return interrupt;
}

Value* VM::handleLabelInterrupt(Value* interrupt) {
    auto* data = neo::core::asInterrupt(interrupt);
    if (data == nullptr) return badBytecode("label interrupt: bad payload");
    uint32_t labelIndex = static_cast<uint32_t>(data->offset);
    const std::string* label = stringOperand(labelIndex);
    if (label == nullptr) {
        return badBytecode("label interrupt: label index out of range");
    }

    bool isBreak = data->kind == neo::core::InterruptKind::Break;
    size_t targetIndex = labelStack_.size();
    for (size_t i = labelStack_.size(); i > 0; --i) {
        const LabelFrame& frame = labelStack_[i - 1];
        if (!isBreak && frame.startPc == 0) continue;
        if (!label->empty() && frame.label != *label) continue;
        targetIndex = i - 1;
        break;
    }
    if (targetIndex == labelStack_.size()) {
        return badBytecode(isBreak ? "break: label not found"
                                   : "continue: loop label not found");
    }

    // 先消费目标 try 深度之上的 finally/帧；目标 label 在 try 外，索引保持有效。
    const size_t targetTryDepth = labelStack_[targetIndex].tryDepth;
    while (tryStack_.size() > targetTryDepth) {
        TryFrame& frame = tryStack_.back();
        if (frame.finallyStart != 0) {
            uint32_t finallyPc = frame.finallyStart;
            frame.finallyStart = 0;
            frame.res = interrupt;
            trackRes(frame, interrupt);
            restoreFrame(frame);
            pc_ = finallyPc;
            return nullptr;
        }
        restoreFrame(frame);
        tryStack_.pop_back();
    }

    const LabelFrame target = labelStack_[targetIndex];
    while (ctx_->current() != nullptr && ctx_->current() != target.scope) {
        ctx_->popScope();
    }
    if (stack_.size() > target.stackDepth) stack_.resize(target.stackDepth);

    if (isBreak) {
        labelStack_.resize(targetIndex);
        pc_ = target.endPc;
    } else {
        labelStack_.resize(targetIndex + 1);
        pc_ = target.startPc;
    }
    return nullptr;
}

// —— 作用域 ——

Value* VM::opPushScope() {
    if (ctx_->pushNewScope(ctx_->current()) == nullptr) return ctx_->oomValue();
    return nullptr;
}

Value* VM::opPopScope() {
    ctx_->popScope();
    return nullptr;
}

Value* VM::opIterationEnv() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("iteration_env: truncated count");
    }
    uint32_t count = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (count > (bytes.size() - pc_) / 4) {
        return badBytecode("iteration_env: truncated names");
    }
    std::vector<const std::string*> names;
    names.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t index = readU32(bytes.data() + pc_);
        pc_ += 4;
        const std::string* name = stringOperand(index);
        if (name == nullptr) {
            return badBytecode("iteration_env: name index out of range");
        }
        names.push_back(name);
    }

    Scope* parent = ctx_->current();
    Scope* child = ctx_->pushNewScope(parent);
    if (child == nullptr) return ctx_->oomValue();
    for (const std::string* name : names) {
        const Scope::Binding* binding = parent->lookupBinding(*name);
        if (binding == nullptr) continue;
        // 每次迭代用独立 Value：闭包捕获本次迭代的槽位，后续迭代的赋值
        // 只改写新 Value，不会回写已捕获的旧 Value。
        Value* copy = ctx_->cloneValue(binding->value);
        if (copy == ctx_->oomValue()) return copy;
        child->define(*name, copy, binding->mutableBinding, binding->initialized);
    }
    return nullptr;
}

Value* VM::opCommitIterationEnv() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("commit_iteration_env: truncated count");
    }
    uint32_t count = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (count > (bytes.size() - pc_) / 4) {
        return badBytecode("commit_iteration_env: truncated names");
    }
    std::vector<const std::string*> names;
    names.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t index = readU32(bytes.data() + pc_);
        pc_ += 4;
        const std::string* name = stringOperand(index);
        if (name == nullptr) {
            return badBytecode("commit_iteration_env: name index out of range");
        }
        names.push_back(name);
    }

    Scope* current = ctx_->current();
    Scope* parent = current->parent();
    if (parent == nullptr) {
        return badBytecode("commit_iteration_env: no parent scope");
    }
    for (const std::string* name : names) {
        const Scope::Binding* source = current->lookupBinding(*name);
        if (source == nullptr) continue;
        Scope::Binding* target = parent->lookupBinding(*name);
        if (target == nullptr) continue;
        // 下一轮种子必须是新 Value：本次迭代的 Value 可能已被闭包捕获，
        // update 的原地赋值不能改到闭包看到的值。
        Value* seed = ctx_->cloneValue(source->value);
        if (seed == ctx_->oomValue()) return seed;
        target->value = seed;
        target->initialized = source->initialized;
        parent->add(seed);
    }
    ctx_->popScope();
    return nullptr;
}

// —— 函数定义 ——

Value* VM::opPushFunction() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 12) {
        return badBytecode("push_function: truncated operand");
    }
    uint32_t entryPc = readU32(bytes.data() + pc_);
    uint32_t paramCount = readU32(bytes.data() + pc_ + 4);
    uint32_t flags = readU32(bytes.data() + pc_ + 8);
    pc_ += 12;

    Value* fn = ctx_->createScriptFunction(
        code_, entryPc, paramCount, (flags & kFunctionHasRest) != 0,
        (flags & kFunctionConstructible) != 0,
        (flags & kFunctionHasPrototype) != 0);
    if (fn == ctx_->oomValue()) return fn;
    if ((flags & kFunctionGenerator) != 0) {
        ctx_->setFunctionGenerator(fn, true);
    }
    if ((flags & kFunctionAsync) != 0) {
        ctx_->setFunctionAsync(fn, true);
    }
    if ((flags & kFunctionAsyncGenerator) != 0) {
        ctx_->setFunctionAsyncGenerator(fn, true);
    }
    push(fn);
    return nullptr;
}

Value* VM::opSetClosure() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("set_closure: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) return badBytecode("set_closure: name index out of range");
    if (stack_.empty()) return badBytecode("set_closure: operand stack underflow");

    Value* result = ctx_->setClosure(top(), *name);
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opSetFuncName() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("set_func_name: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) return badBytecode("set_func_name: name index out of range");
    if (stack_.empty()) return badBytecode("set_func_name: operand stack underflow");

    ctx_->setFunctionName(top(), *name);
    return nullptr;
}

Value* VM::opBindThis() {
    if (stack_.size() < 2) {
        return badBytecode("bind_this: operand stack underflow");
    }
    Value* self = pop();
    ctx_->bindFunctionThis(top(), self);
    return nullptr;
}

Value* VM::opLoadThis() {
    Value* value = ctx_->getThis();
    if (detail::isErrorValue(value)) return fail(value);
    push(value);
    return nullptr;
}

Value* VM::opStoreThis() {
    if (stack_.empty()) return badBytecode("store_this: operand stack underflow");
    ctx_->initializeThis(pop());
    return nullptr;
}

// —— 调用 ——

Value* VM::opCall() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("call: truncated operand");
    uint32_t argc = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < static_cast<size_t>(argc) + 1) {
        return badBytecode("call: operand stack underflow");
    }

    std::vector<Value*> args(argc);
    for (uint32_t i = argc; i > 0; --i) args[i - 1] = pop();
    Value* fn = pop();

    Value* result = ctx_->call(fn, ctx_->createUndefined(), argc, args.data());
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opCallMethod() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("call_method: truncated operand");
    uint32_t argc = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < static_cast<size_t>(argc) + 2) {
        return badBytecode("call_method: operand stack underflow");
    }

    std::vector<Value*> args(argc);
    for (uint32_t i = argc; i > 0; --i) args[i - 1] = pop();
    Value* fn = pop();
    Value* receiver = pop();

    Value* result = ctx_->call(fn, receiver, argc, args.data());
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opCallSpread() {
    if (stack_.size() < 2) {
        return badBytecode("call_spread: operand stack underflow");
    }
    Value* argsArray = pop();
    Value* fn = pop();
    std::vector<Value*> args;
    if (Value* err = readArrayElements(ctx_, argsArray, args)) return fail(err);
    Value* result =
        ctx_->call(fn, ctx_->createUndefined(),
                   static_cast<size_t>(args.size()), args.data());
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opCallMethodSpread() {
    if (stack_.size() < 3) {
        return badBytecode("call_method_spread: operand stack underflow");
    }
    Value* argsArray = pop();
    Value* fn = pop();
    Value* receiver = pop();
    std::vector<Value*> args;
    if (Value* err = readArrayElements(ctx_, argsArray, args)) return fail(err);
    Value* result = ctx_->call(fn, receiver, static_cast<size_t>(args.size()),
                               args.data());
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opNew() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("new: truncated operand");
    uint32_t argc = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < static_cast<size_t>(argc) + 1) {
        return badBytecode("new: operand stack underflow");
    }

    std::vector<Value*> args(argc);
    for (uint32_t i = argc; i > 0; --i) args[i - 1] = pop();
    Value* ctor = pop();

    Value* result = ctx_->construct(ctor, argc, args.data());
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opNewSpread() {
    if (stack_.size() < 2) {
        return badBytecode("new_spread: operand stack underflow");
    }
    Value* argsArray = pop();
    Value* ctor = pop();
    std::vector<Value*> args;
    if (Value* err = readArrayElements(ctx_, argsArray, args)) return fail(err);
    Value* result =
        ctx_->construct(ctor, static_cast<size_t>(args.size()), args.data());
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opLoadNewTarget() {
    Value* value = ctx_->current() != nullptr
                       ? ctx_->current()->lookupNewTarget()
                       : nullptr;
    push(value != nullptr ? value : ctx_->createUndefined());
    return nullptr;
}

// —— 结束 ——

Value* VM::opYield() {
    if (stack_.empty()) return badBytecode("yield: operand stack underflow");
    Value* value = pop();
    return ctx_->createInterrupt(neo::core::InterruptKind::Yield, pc_, value);
}

Value* VM::opYieldDelegate() {
    if (stack_.empty()) {
        return badBytecode("yield_delegate: operand stack underflow");
    }
    Value* iterable = pop();
    Value* iterator = detail::getIterator(ctx_, iterable);
    if (detail::isErrorValue(iterator)) return fail(iterator);
    delegateIterator_ = iterator;
    Value* pending = delegateStep(ctx_->createUndefined());
    if (pending != nullptr) return pending;
    return nullptr;
}

Value* VM::delegateStep(Value* sent) {
    Value* iterator = delegateIterator_;
    if (iterator == nullptr) return badBytecode("yield_delegate: missing iterator");

    auto finish = [this]() { delegateIterator_ = nullptr; };
    auto stepResult = [this](Value* result) -> Value* {
        Value* done = ctx_->getProperty(result, "done");
        if (detail::isErrorValue(done)) {
            delegateIterator_ = nullptr;
            return fail(done);
        }
        Value* value = ctx_->getProperty(result, "value");
        if (detail::isErrorValue(value)) {
            delegateIterator_ = nullptr;
            return fail(value);
        }
        if (ctx_->toBoolean(done)) {
            delegateIterator_ = nullptr;
            push(value);
            return nullptr;
        }
        return ctx_->createInterrupt(neo::core::InterruptKind::Yield, pc_, value);
    };

    bool isThrow = false;
    bool isReturn = false;
    Value* payload = nullptr;
    if (sent != nullptr && detail::isInterruptValue(sent)) {
        auto* data = neo::core::asInterrupt(sent);
        if (data != nullptr) {
            isThrow = data->kind == neo::core::InterruptKind::Throw;
            isReturn = data->kind == neo::core::InterruptKind::Return;
            payload = data->value;
        }
    }

    if (isThrow) {
        Value* method = ctx_->getProperty(iterator, "throw");
        if (detail::isErrorValue(method)) {
            finish();
            return fail(method);
        }
        if (!detail::isCallable(method)) {
            detail::iteratorClose(ctx_, iterator, nullptr);
            finish();
            return fail(detail::typeError(
                ctx_, "The iterator does not provide a 'throw' method"));
        }
        Value* result = ctx_->call(method, iterator, 1, &payload);
        if (detail::isErrorValue(result)) {
            finish();
            return fail(result);
        }
        return stepResult(result);
    }

    if (isReturn) {
        Value* method = ctx_->getProperty(iterator, "return");
        if (detail::isErrorValue(method)) {
            finish();
            return fail(method);
        }
        if (!detail::isCallable(method)) {
            finish();
            return unwindReturn(payload);
        }
        Value* result = ctx_->call(method, iterator, 1, &payload);
        if (detail::isErrorValue(result)) {
            finish();
            return fail(result);
        }
        Value* done = ctx_->getProperty(result, "done");
        if (detail::isErrorValue(done)) {
            finish();
            return fail(done);
        }
        Value* value = ctx_->getProperty(result, "value");
        if (detail::isErrorValue(value)) {
            finish();
            return fail(value);
        }
        if (ctx_->toBoolean(done)) {
            finish();
            return unwindReturn(value);
        }
        return ctx_->createInterrupt(neo::core::InterruptKind::Yield, pc_, value);
    }

    Value* method = ctx_->getProperty(iterator, "next");
    if (detail::isErrorValue(method)) {
        finish();
        return fail(method);
    }
    if (!detail::isCallable(method)) {
        finish();
        return fail(detail::typeError(
            ctx_, "The iterator does not provide a 'next' method"));
    }
    Value* arg = sent != nullptr ? sent : ctx_->createUndefined();
    Value* result = ctx_->call(method, iterator, 1, &arg);
    if (detail::isErrorValue(result)) {
        finish();
        return fail(result);
    }
    return stepResult(result);
}

Value* VM::opAwait() {
    if (stack_.empty()) return badBytecode("await: operand stack underflow");
    Value* value = pop();
    return ctx_->createInterrupt(neo::core::InterruptKind::Await, pc_, value);
}

Value* VM::opRet() {
    Value* value = stack_.empty() ? ctx_->createUndefined() : pop();
    return unwindReturn(value);
}

Value* VM::opHalt() {
    return ctx_->createUndefined();
}

Value* VM::opPushClass() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 12) {
        return badBytecode("push_class: truncated operand");
    }
    uint32_t entryPc = readU32(bytes.data() + pc_);
    uint32_t paramCount = readU32(bytes.data() + pc_ + 4);
    uint32_t flags = readU32(bytes.data() + pc_ + 8);
    pc_ += 12;

    if (stack_.empty()) return badBytecode("push_class: missing super constructor");
    Value* superCtor = pop();
    bool isDerived = (flags & kClassDerived) != 0;
    Value* ctor = ctx_->createScriptClassConstructor(
        code_, entryPc, paramCount, (flags & kFunctionHasRest) != 0, isDerived,
        superCtor, "");
    if (detail::isErrorValue(ctor)) return fail(ctor);
    push(ctor);
    return nullptr;
}

Value* VM::opSetStrict() {
    if (stack_.empty()) return badBytecode("set_strict: operand stack underflow");
    ctx_->setFunctionStrict(top());
    return nullptr;
}

Value* VM::opInitInstanceElements() {
    Value* thisValue = ctx_->getThis();
    if (detail::isErrorValue(thisValue)) return fail(thisValue);
    Value* result =
        ctx_->initializeInstanceElements(thisValue, ctx_->currentFunction());
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opConstructSuper() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("construct_super: truncated operand");
    }
    uint32_t argc = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < argc) {
        return badBytecode("construct_super: operand stack underflow");
    }
    std::vector<Value*> args(argc);
    for (uint32_t i = argc; i > 0; --i) args[i - 1] = pop();

    Value* superCtor = ctx_->classSuperConstructor(ctx_->currentFunction());
    Value* result = ctx_->constructSuper(superCtor, ctx_->newTarget(), argc,
                                         args.empty() ? nullptr : args.data());
    if (detail::isErrorValue(result)) return fail(result);
    // super() 返回后初始化当前 class 的实例元素（字段/私有 brand）。
    Value* init =
        ctx_->initializeInstanceElements(result, ctx_->currentFunction());
    if (detail::isErrorValue(init)) return fail(init);
    push(result);
    return nullptr;
}

Value* VM::opConstructSuperSpread() {
    if (stack_.empty()) {
        return badBytecode("construct_super_spread: operand stack underflow");
    }
    Value* argsArray = pop();
    std::vector<Value*> args;
    if (Value* err = readArrayElements(ctx_, argsArray, args)) return fail(err);

    Value* superCtor = ctx_->classSuperConstructor(ctx_->currentFunction());
    Value* result = ctx_->constructSuper(superCtor, ctx_->newTarget(),
                                         args.size(), args.data());
    if (detail::isErrorValue(result)) return fail(result);
    Value* init =
        ctx_->initializeInstanceElements(result, ctx_->currentFunction());
    if (detail::isErrorValue(init)) return fail(init);
    push(result);
    return nullptr;
}

Value* VM::opGetSuperProp() {
    if (stack_.empty()) return badBytecode("get_super_prop: operand stack underflow");
    Value* key = pop();
    Value* homeObject = ctx_->functionHomeObject(ctx_->currentFunction());
    Value* receiver = ctx_->getThis();
    if (detail::isErrorValue(receiver)) return fail(receiver);
    Value* result = ctx_->getSuperProperty(homeObject, key, receiver);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opSetSuperProp() {
    if (stack_.size() < 2) {
        return badBytecode("set_super_prop: operand stack underflow");
    }
    Value* value = pop();
    Value* key = pop();
    Value* homeObject = ctx_->functionHomeObject(ctx_->currentFunction());
    Value* receiver = ctx_->getThis();
    if (detail::isErrorValue(receiver)) return fail(receiver);
    Value* result =
        ctx_->setSuperProperty(homeObject, key, value, receiver);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opDefineClassMethod() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("define_class_method: truncated operand");
    }
    uint32_t flags = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < 3) {
        return badBytecode("define_class_method: operand stack underflow");
    }
    Value* fn = pop();
    Value* key = pop();
    Value* ctor = pop();
    bool isStatic = (flags & 1u) != 0;
    uint32_t kind = (flags >> 1) & 3u;
    Value* result = ctx_->defineClassMethod(ctor, key, fn, isStatic, kind);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opDefineClassField() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("define_class_field: truncated operand");
    }
    uint32_t flags = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < 3) {
        return badBytecode("define_class_field: operand stack underflow");
    }
    Value* initializer = pop();
    Value* key = pop();
    Value* ctor = pop();
    Value* result =
        ctx_->defineClassField(ctor, key, initializer, (flags & 1u) != 0);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opRestArgs() {
    // 序言栈上此时只剩 argv[paramCount..]：栈顶是第一个 rest 实参。
    Value* array = ctx_->createArray();
    if (array == ctx_->oomValue()) return array;
    for (size_t i = stack_.size(); i > 0; --i) {
        Value* result = ctx_->arrayAppend(array, stack_[i - 1]);
        if (detail::isErrorValue(result)) return fail(result);
    }
    stack_.clear();
    push(array);
    return nullptr;
}

}  // namespace neo::vm
