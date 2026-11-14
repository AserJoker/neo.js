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
#include "core/OperatorKind.h"
#include "core/Scope.h"
#include "core/types/Exception.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <utility>

namespace neo::vm {

namespace detail = neo::core::detail;

using neo::core::BinaryOperator;
using neo::core::ErrorKind;
using neo::core::IContext;
using neo::core::SourcePosition;
using neo::core::Value;

namespace {

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
        case OpCode::Add:           return "add";
        case OpCode::Sub:           return "sub";
        case OpCode::Mul:           return "mul";
        case OpCode::Div:           return "div";
        case OpCode::Lt:            return "lt";
        case OpCode::Gt:            return "gt";
        case OpCode::Le:            return "le";
        case OpCode::Ge:            return "ge";
        case OpCode::Jmp:           return "jmp";
        case OpCode::JTrue:         return "jtrue";
        case OpCode::JFalse:        return "jfalse";
        case OpCode::TryBegin:      return "try_begin";
        case OpCode::TryEnd:        return "try_end";
        case OpCode::Throw:         return "throw";
        case OpCode::PushScope:     return "push_scope";
        case OpCode::PopScope:      return "pop_scope";
        case OpCode::PushFunction:  return "push_function";
        case OpCode::SetClosure:    return "set_closure";
        case OpCode::SetFuncName:   return "set_func_name";
        case OpCode::BindThis:      return "bind_this";
        case OpCode::LoadThis:      return "load_this";
        case OpCode::StoreThis:     return "store_this";
        case OpCode::Call:          return "call";
        case OpCode::CallMethod:    return "call_method";
        case OpCode::Ret:           return "ret";
        case OpCode::Halt:          return "hlt";
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
        &VM::opAdd,
        &VM::opSub,
        &VM::opMul,
        &VM::opDiv,
        &VM::opLt,
        &VM::opGt,
        &VM::opLe,
        &VM::opGe,
        &VM::opJmp,
        &VM::opJTrue,
        &VM::opJFalse,
        &VM::opTryBegin,
        &VM::opTryEnd,
        &VM::opThrow,
        &VM::opPushScope,
        &VM::opPopScope,
        &VM::opPushFunction,
        &VM::opSetClosure,
        &VM::opSetFuncName,
        &VM::opBindThis,
        &VM::opLoadThis,
        &VM::opStoreThis,
        &VM::opCall,
        &VM::opCallMethod,
        &VM::opRet,
        &VM::opHalt,
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
    ctx_->setExceptionPosition(error, currentPosition());
    // Fatal 不可捕获（ADR-017）：不进入 try 展开。
    if (error->type()->kind() == neo::core::TypeKind::Fatal) return error;
    return dispatchException(error);
}

void VM::restoreFrame(const TryFrame& frame) {
    if (stack_.size() > frame.stackDepth) stack_.resize(frame.stackDepth);
    while (ctx_->current() != nullptr && ctx_->current() != frame.scope) {
        ctx_->popScope();
    }
}

Value* VM::unwindReturn(Value* value) {
    while (!tryStack_.empty()) {
        TryFrame& frame = tryStack_.back();
        if (frame.phase == TryPhase::Finally) {
            // finally 内的 return 覆盖待处理 completion，直接到 try_end。
            frame.pending = PendingKind::Return;
            frame.pendingValue = value;
            pc_ = frame.endTryPc;
            return nullptr;
        }
        if (frame.finallyPc != 0) {
            restoreFrame(frame);
            frame.phase = TryPhase::Finally;
            frame.pending = PendingKind::Return;
            frame.pendingValue = value;
            pc_ = frame.finallyPc;
            return nullptr;
        }
        // catch 不处理 return；无 finally 时继续向外展开。
        restoreFrame(frame);
        tryStack_.pop_back();
    }
    return value;
}

Value* VM::dispatchException(Value* error) {
    while (!tryStack_.empty()) {
        TryFrame& frame = tryStack_.back();
        if (frame.phase == TryPhase::Finally) {
            // finally 抛出的异常覆盖待处理 completion，由 try_end 向外展开。
            frame.pending = PendingKind::Throw;
            frame.pendingValue = error;
            pc_ = frame.endTryPc;
            return nullptr;
        }
        if (frame.phase == TryPhase::Try && frame.catchPc != 0) {
            restoreFrame(frame);
            frame.phase = TryPhase::Catch;
            // catch 绑定的是被抛出的值（Exception.child），不是 Exception 通道本身。
            push(static_cast<neo::core::ExceptionData*>(error->data())->child);
            pc_ = frame.catchPc;
            return nullptr;
        }
        if (frame.finallyPc != 0) {
            restoreFrame(frame);
            frame.phase = TryPhase::Finally;
            frame.pending = PendingKind::Throw;
            frame.pendingValue = error;
            pc_ = frame.finallyPc;
            return nullptr;
        }
        restoreFrame(frame);
        tryStack_.pop_back();
    }
    return error;
}

Value* VM::badBytecode(const char* message) {
    return fail(ctx_->createErrorException(ErrorKind::Error, message));
}

Value* VM::run(uint32_t entryPc) {
    pc_ = entryPc;
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
        if (signal != nullptr) return signal;
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

// —— 异常 ——

Value* VM::opTryBegin() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 12) {
        return badBytecode("try_begin: truncated operand");
    }
    TryFrame frame;
    frame.catchPc = readU32(bytes.data() + pc_);
    frame.finallyPc = readU32(bytes.data() + pc_ + 4);
    frame.endTryPc = readU32(bytes.data() + pc_ + 8);
    pc_ += 12;
    frame.stackDepth = stack_.size();
    frame.scope = ctx_->current();
    tryStack_.push_back(frame);
    return nullptr;
}

Value* VM::opTryEnd() {
    if (tryStack_.empty()) return badBytecode("try_end: try stack underflow");
    TryFrame& frame = tryStack_.back();
    if (frame.phase != TryPhase::Finally && frame.finallyPc != 0) {
        // 正常完成 try/catch：进入 finally，帧保留到 finally 结束。
        frame.phase = TryPhase::Finally;
        pc_ = frame.finallyPc;
        return nullptr;
    }

    PendingKind pending = frame.pending;
    Value* pendingValue = frame.pendingValue;
    tryStack_.pop_back();
    if (pending == PendingKind::None) return nullptr;
    if (pending == PendingKind::Return) return unwindReturn(pendingValue);
    return dispatchException(pendingValue);
}

Value* VM::opThrow() {
    if (stack_.empty()) return badBytecode("throw: operand stack underflow");
    Value* value = pop();
    Value* exception = ctx_->createException(value);
    if (exception == ctx_->oomValue()) return exception;
    return fail(exception);
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

// —— 结束 ——

Value* VM::opRet() {
    Value* value = stack_.empty() ? ctx_->createUndefined() : pop();
    return unwindReturn(value);
}

Value* VM::opHalt() {
    return ctx_->createUndefined();
}

}  // namespace neo::vm
