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

#include "bigint/BigInt.h"

#include <cstdio>
#include <string>
#include <utility>

namespace neo::vm {

namespace detail = neo::core::detail;

using neo::core::BinaryOperator;
using neo::core::ErrorKind;
using neo::core::IContext;
using neo::core::Scope;
using neo::core::SourcePosition;
using neo::core::TypeKind;
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
        case OpCode::PushBigInt:    return "push_bigint";
        case OpCode::Pop:           return "pop";
        case OpCode::Dup:           return "dup";
        case OpCode::Clone:         return "clone";
        case OpCode::Swap:          return "swap";
        case OpCode::Define:        return "define";
        case OpCode::Initialize:    return "initialize";
        case OpCode::Load:          return "load";
        case OpCode::LoadSoft:      return "load_soft";
        case OpCode::DefineIfMissing: return "define_if_missing";
        case OpCode::Store:         return "store";
        case OpCode::StoreVar:      return "store_var";
        case OpCode::ResolveWithRef: return "resolve_with_ref";
        case OpCode::StoreWithRef:  return "store_with_ref";
        case OpCode::LoadWithRef:   return "load_with_ref";
        case OpCode::StoreWithRefKeep: return "store_with_ref_keep";
        case OpCode::GetProp:       return "get_prop";
        case OpCode::SetProp:       return "set_prop";
        case OpCode::NewObject:     return "new_object";
        case OpCode::NewArray:      return "new_array";
        case OpCode::DefineProp:    return "define_prop";
        case OpCode::DefineAccessor: return "define_accessor";
        case OpCode::SetHomeObject: return "set_home_object";
        case OpCode::InheritHomeObject: return "inherit_home_object";
        case OpCode::SetProtoLiteral: return "set_proto_literal";
        case OpCode::TemplateObject: return "template_object";
        case OpCode::ArrayAppend:   return "array_append";
        case OpCode::ArrayGrow:     return "array_grow";
        case OpCode::ArraySpread:   return "array_spread";
        case OpCode::ObjectSpread:  return "object_spread";
        case OpCode::ObjectRest:    return "object_rest";
        case OpCode::ObjectRestDynamic: return "object_rest_dynamic";
        case OpCode::ForInKeys:     return "for_in_keys";
        case OpCode::IsEnumerableOwn: return "is_enumerable_own";
        case OpCode::GetIterator:   return "get_iterator";
        case OpCode::IteratorNext:  return "iterator_next";
        case OpCode::IteratorNextCached: return "iterator_next_cached";
        case OpCode::IteratorClose: return "iterator_close";
        case OpCode::Add:           return "add";
        case OpCode::Sub:           return "sub";
        case OpCode::Mul:           return "mul";
        case OpCode::Div:           return "div";
        case OpCode::Inc:           return "inc";
        case OpCode::Dec:           return "dec";
        case OpCode::ToNumeric:     return "to_numeric";
        case OpCode::ResolveSlot:   return "resolve_slot";
        case OpCode::LoadSlotRef:   return "load_slot_ref";
        case OpCode::StoreSlotRef:  return "store_slot_ref";
        case OpCode::MarkParamInit: return "mark_param_init";
        case OpCode::DirectEvalTail: return "direct_eval_tail";
        case OpCode::InheritNewTarget: return "inherit_new_target";
        case OpCode::Lt:            return "lt";
        case OpCode::Gt:            return "gt";
        case OpCode::Le:            return "le";
        case OpCode::Ge:            return "ge";
        case OpCode::StrictEqual:   return "strict_equal";
        case OpCode::In:            return "in";
        case OpCode::InstanceOf:    return "instance_of";
        case OpCode::Binary:        return "binary";
        case OpCode::Unary:         return "unary";
        case OpCode::ToString:      return "to_string";
        case OpCode::Jmp:           return "jmp";
        case OpCode::JTrue:         return "jtrue";
        case OpCode::JFalse:        return "jfalse";
        case OpCode::JNotNullish:   return "jnotnullish";
        case OpCode::TryBegin:      return "try_begin";
        case OpCode::TryBeginInternal: return "try_begin_internal";
        case OpCode::TryBodyEnd:    return "try_body_end";
        case OpCode::CatchEnd:      return "catch_end";
        case OpCode::FinallyEnd:    return "finally_end";
        case OpCode::Throw:         return "throw";
        case OpCode::PushLabel:     return "push_label";
        case OpCode::PopLabel:      return "pop_label";
        case OpCode::PopLabelIfTop: return "pop_label_if_top";
        case OpCode::Break:         return "break";
        case OpCode::Continue:      return "continue";
        case OpCode::BreakValue:    return "break_value";
        case OpCode::ContinueValue: return "continue_value";
        case OpCode::EnableCompletion: return "enable_completion";
        case OpCode::MergeCompletion: return "merge_completion";
        case OpCode::CompletionToUndefined: return "completion_to_undefined";
        case OpCode::UpdateLoopValue: return "update_loop_value";
        case OpCode::LoopEnd:       return "loop_end";
        case OpCode::LabelledEnd:   return "labelled_end";
        case OpCode::LoadCompletionResult: return "load_completion_result";
        case OpCode::UpdatePendingCompletion: return "update_pending_completion";
        case OpCode::SetFunctionLength: return "set_function_length";
        case OpCode::SetFunctionSloppy: return "set_function_sloppy";
        case OpCode::PushScope:     return "push_scope";
        case OpCode::PushVarScope:  return "push_var_scope";
        case OpCode::PopScope:      return "pop_scope";
        case OpCode::PushWithScope: return "push_with_scope";
        case OpCode::IterationEnv:  return "iteration_env";
        case OpCode::CommitIterationEnv: return "commit_iteration_env";
        case OpCode::PushFunction:  return "push_function";
        case OpCode::SetParamNames: return "set_param_names";
        case OpCode::SetClosure:    return "set_closure";
        case OpCode::SetFuncName:   return "set_func_name";
        case OpCode::SetFuncNameBinding: return "set_func_name_binding";
        case OpCode::SetFuncNameIfAbsent: return "set_func_name_if_absent";
        case OpCode::SetFuncNameFromKey:  return "set_func_name_from_key";
        case OpCode::ThrowReferenceError: return "throw_reference_error";
        case OpCode::BodyStart:     return "body_start";
        case OpCode::ThrowTypeError: return "throw_type_error";
        case OpCode::ToPropertyKey: return "to_property_key";
        case OpCode::BindThis:      return "bind_this";
        case OpCode::LoadThis:      return "load_this";
        case OpCode::StoreThis:     return "store_this";
        case OpCode::Call:          return "call";
        case OpCode::CallMethod:    return "call_method";
        case OpCode::CallSpread:    return "call_spread";
        case OpCode::CallMethodSpread: return "call_method_spread";
        case OpCode::TailCall:      return "tail_call";
        case OpCode::TailCallMethod: return "tail_call_method";
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
        case OpCode::MarkCatchScope: return "mark_catch_scope";
        case OpCode::InitInstanceElements: return "init_instance_elements";
        case OpCode::ConstructSuper: return "construct_super";
        case OpCode::ConstructSuperSpread: return "construct_super_spread";
        case OpCode::GetSuperProp:  return "get_super_prop";
        case OpCode::SetSuperProp:  return "set_super_prop";
        case OpCode::GetSuperBase:  return "get_super_base";
        case OpCode::GetSuperPropFromBase: return "get_super_prop_from_base";
        case OpCode::SetSuperPropFromBase: return "set_super_prop_from_base";
        case OpCode::SetSuperPropFromBaseKeep:
            return "set_super_prop_from_base_keep";
        case OpCode::DefineClassMethod: return "define_class_method";
        case OpCode::DefineClassField: return "define_class_field";
        case OpCode::YieldDelegate: return "yield_delegate";
        case OpCode::Dup2:          return "dup2";
        case OpCode::DeleteProp:    return "delete_prop";
        case OpCode::DeleteIdentifier: return "delete_identifier";
        case OpCode::SetPropKeep:   return "set_prop_keep";
        case OpCode::SetSuperPropKeep: return "set_super_prop_keep";
        case OpCode::DefinePrivateField: return "define_private_field";
        case OpCode::DefinePrivateMethod: return "define_private_method";
        case OpCode::GetPrivateField: return "get_private_field";
        case OpCode::GetPrivateMethod: return "get_private_method";
        case OpCode::GetPrivateGetter: return "get_private_getter";
        case OpCode::SetPrivateField: return "set_private_field";
        case OpCode::SetPrivateSetter: return "set_private_setter";
        case OpCode::SetPrivateFieldKeep: return "set_private_field_keep";
        case OpCode::SetPrivateSetterKeep: return "set_private_setter_keep";
        case OpCode::SetOwnerClass: return "set_owner_class";
        case OpCode::PrivateIn:     return "private_in";
        case OpCode::ImportMeta:    return "import_meta";
        case OpCode::DynamicImport: return "dynamic_import";
        case OpCode::GetAsyncIterator: return "get_async_iterator";
        case OpCode::AsyncIteratorClose: return "async_iterator_close";
        case OpCode::PushRegExp:    return "push_regexp";
        case OpCode::DeclareVar:    return "declare_var";
        case OpCode::DeclareGlobalVar: return "declare_global_var";
        case OpCode::DeclareGlobalFunction: return "declare_global_function";
        case OpCode::CheckGlobalLexical: return "check_global_lexical";
        case OpCode::DefineGlobalLexical: return "define_global_lexical";
        case OpCode::DirectEval:    return "direct_eval";
        case OpCode::DirectEvalSpread: return "direct_eval_spread";
        case OpCode::SetScopeStrict: return "set_scope_strict";
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
        &VM::opPushBigInt,
        &VM::opPop,
        &VM::opDup,
        &VM::opClone,
        &VM::opSwap,
        &VM::opDefine,
        &VM::opInitialize,
        &VM::opLoad,
        &VM::opLoadSoft,
        &VM::opDefineIfMissing,
        &VM::opStore,
        &VM::opStoreVar,
        &VM::opResolveWithRef,
        &VM::opStoreWithRef,
        &VM::opLoadWithRef,
        &VM::opStoreWithRefKeep,
        &VM::opGetProp,
        &VM::opSetProp,
        &VM::opNewObject,
        &VM::opNewArray,
        &VM::opDefineProp,
        &VM::opDefineAccessor,
        &VM::opSetHomeObject,
        &VM::opInheritHomeObject,
        &VM::opSetProtoLiteral,
        &VM::opTemplateObject,
        &VM::opArrayAppend,
        &VM::opArrayGrow,
        &VM::opArraySpread,
        &VM::opObjectSpread,
        &VM::opObjectRest,
        &VM::opObjectRestDynamic,
        &VM::opForInKeys,
        &VM::opIsEnumerableOwn,
        &VM::opGetIterator,
        &VM::opIteratorNext,
        &VM::opIteratorNextCached,
        &VM::opIteratorClose,
        &VM::opAdd,
        &VM::opSub,
        &VM::opMul,
        &VM::opDiv,
        &VM::opInc,
        &VM::opDec,
        &VM::opLt,
        &VM::opGt,
        &VM::opLe,
        &VM::opGe,
        &VM::opStrictEqual,
        &VM::opIn,
        &VM::opUnary,
        &VM::opToString,
        &VM::opJmp,
        &VM::opJTrue,
        &VM::opJFalse,
        &VM::opJNotNullish,
        &VM::opTryBegin,
        &VM::opTryBeginInternal,
        &VM::opTryBodyEnd,
        &VM::opCatchEnd,
        &VM::opFinallyEnd,
        &VM::opThrow,
        &VM::opPushLabel,
        &VM::opPopLabel,
        &VM::opPopLabelIfTop,
        &VM::opBreak,
        &VM::opContinue,
        &VM::opBreakValue,
        &VM::opContinueValue,
        &VM::opEnableCompletion,
        &VM::opMergeCompletion,
        &VM::opCompletionToUndefined,
        &VM::opUpdateLoopValue,
        &VM::opLoopEnd,
        &VM::opLabelledEnd,
        &VM::opLoadCompletionResult,
        &VM::opUpdatePendingCompletion,
        &VM::opSetFunctionLength,
        &VM::opSetFunctionSloppy,
        &VM::opPushScope,
        &VM::opPushVarScope,
        &VM::opPopScope,
        &VM::opPushWithScope,
        &VM::opIterationEnv,
        &VM::opCommitIterationEnv,
        &VM::opPushFunction,
        &VM::opSetParamNames,
        &VM::opSetClosure,
        &VM::opSetFuncName,
        &VM::opSetFuncNameBinding,
        &VM::opBindThis,
        &VM::opLoadThis,
        &VM::opStoreThis,
        &VM::opCall,
        &VM::opCallMethod,
        &VM::opCallSpread,
        &VM::opCallMethodSpread,
        &VM::opTailCall,
        &VM::opTailCallMethod,
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
        &VM::opMarkCatchScope,
        &VM::opInitInstanceElements,
        &VM::opConstructSuper,
        &VM::opConstructSuperSpread,
        &VM::opGetSuperProp,
        &VM::opSetSuperProp,
        &VM::opGetSuperBase,
        &VM::opGetSuperPropFromBase,
        &VM::opSetSuperPropFromBase,
        &VM::opSetSuperPropFromBaseKeep,
        &VM::opDefineClassMethod,
        &VM::opDefineClassField,
        &VM::opYieldDelegate,
        &VM::opDup2,
        &VM::opDeleteProp,
        &VM::opDeleteIdentifier,
        &VM::opSetPropKeep,
        &VM::opSetSuperPropKeep,
        &VM::opDefinePrivateField,
        &VM::opDefinePrivateMethod,
        &VM::opGetPrivateField,
        &VM::opGetPrivateMethod,
        &VM::opGetPrivateGetter,
        &VM::opSetPrivateField,
        &VM::opSetPrivateSetter,
        &VM::opSetPrivateFieldKeep,
        &VM::opSetPrivateSetterKeep,
        &VM::opSetOwnerClass,
        &VM::opPrivateIn,
        &VM::opImportMeta,
        &VM::opDynamicImport,
        &VM::opGetAsyncIterator,
        &VM::opAsyncIteratorClose,
        &VM::opPushRegExp,
        &VM::opDeclareVar,
        &VM::opDeclareGlobalVar,
        &VM::opDeclareGlobalFunction,
        &VM::opCheckGlobalLexical,
        &VM::opDefineGlobalLexical,
        &VM::opDirectEval,
        &VM::opDirectEvalSpread,
        &VM::opSetScopeStrict,
        &VM::opInstanceOf,
        &VM::opBinary,
        &VM::opSetFuncNameIfAbsent,
        &VM::opSetFuncNameFromKey,
        &VM::opThrowReferenceError,
        &VM::opBodyStart,
        &VM::opThrowTypeError,
        &VM::opToPropertyKey,
        &VM::opToNumeric,
        &VM::opResolveSlot,
        &VM::opLoadSlotRef,
        &VM::opStoreSlotRef,
        &VM::opMarkParamInit,
        &VM::opDirectEvalTail,
        &VM::opInheritNewTarget,
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
            // 进入 finally 后不得再回到同一 try 的 catch。
            frame.catchStart = 0;
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
            // 进入 finally 后不得再回到同一 try 的 catch。
            frame.catchStart = 0;
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
        // 进入 finally 后不得再回到同一 try 的 catch。
        frame.catchStart = 0;
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
    // try/finally 返回 UpdateEmpty(F, undefined)：break/continue 的 EMPTY
    // 完成值在离开 try 时归一化为 undefined。
    if (completionMode_ && frame.normalizeAbruptCompletion &&
        detail::isInterruptValue(res)) {
        auto* data = neo::core::asInterrupt(res);
        if (data != nullptr &&
            (data->kind == neo::core::InterruptKind::Break ||
             data->kind == neo::core::InterruptKind::Continue) &&
            data->value != nullptr &&
            data->value->type()->kind() == neo::core::TypeKind::Uninitialized) {
            data->value = ctx_->createUndefined();
        }
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
        if (pending != nullptr) {
            // 被委托迭代器协议错误（如缺少 throw 方法）是普通异常，
            // 须进入 yield* 所在 try 帧展开，而不是直接逃逸到调用方。
            if (pending->type()->kind() == neo::core::TypeKind::Exception) {
                Value* error = dispatchException(pending);
                if (error != nullptr) return error;
            } else {
                return pending;
            }
        }
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
    // 脚本默认 sloppy：为本次执行建立临时作用域，只有源码含 "use strict"
    // 时才标注 strict。作用域只承载执行模式，声明仍落 GlobalScope/global object。
    Scope* scriptScope = nullptr;
    if (code_->isScriptProgram && ctx_->current() == ctx_->global()) {
        scriptScope = ctx_->pushNewScope(ctx_->global());
        if (scriptScope == nullptr) return ctx_->oomValue();
        if (code_->strict) scriptScope->setFlag("strict");
    }
    pc_ = entryPc;
    instructionPc_ = entryPc;
    Value* result = execute();
    // 异常路径可能遗留未弹出的块级作用域：先回到脚本作用域再弹出。
    if (scriptScope != nullptr) {
        while (ctx_->current() != nullptr && ctx_->current() != scriptScope) {
            ctx_->popScope();
        }
        if (ctx_->current() == scriptScope) ctx_->popScope();
    }
    return result;
}

Value* VM::execute() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (pc_ > bytes.size()) {
        return badBytecode("run: entry pc out of range");
    }

    const Handler* table = handlers();
    while (pc_ < bytes.size()) {
        // 停止标记：worker terminate / 调试器复用。每条指令后检查一次；
        // 终止是不可捕获的控制信号，不进入 try 展开。
        if (ctx_->isStopRequested()) {
            return ctx_->createInterrupt(neo::core::InterruptKind::Terminate,
                                         pc_, nullptr);
        }
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

Value* VM::opPushBigInt() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 8) {
        return badBytecode("push_bigint: truncated operand");
    }
    uint32_t index = readU32(bytes.data() + pc_);
    pc_ += 4;
    uint32_t radix = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* digits = stringOperand(index);
    if (digits == nullptr) {
        return badBytecode("push_bigint: constant index out of range");
    }
    neo::bigint::BigInt value;
    if (!neo::bigint::BigInt::parse(*digits, static_cast<int>(radix), value)) {
        return badBytecode("push_bigint: invalid literal");
    }
    push(ctx_->createBigInt(value));
    return nullptr;
}

Value* VM::opPushRegExp() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 12) {
        return badBytecode("push_regexp: truncated operand");
    }
    const uint32_t patternIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const uint32_t flagsIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const uint32_t programIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* pattern = stringOperand(patternIndex);
    const std::string* flags = stringOperand(flagsIndex);
    if (pattern == nullptr || flags == nullptr) {
        return badBytecode("push_regexp: constant index out of range");
    }
    Value* regexp = nullptr;
    if (programIndex == Code::kNoRegexProgram) {
        regexp = ctx_->createRegExp(*pattern, *flags);
    } else {
        if (programIndex >= code_->regexPrograms.size()) {
            return badBytecode("push_regexp: program index out of range");
        }
        regexp = ctx_->createRegExp(*pattern, *flags,
                                    code_->regexPrograms[programIndex]);
    }
    if (detail::isErrorValue(regexp)) return regexp;
    push(regexp);
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
    // delete 的基值先 ToObject：null/undefined 抛 TypeError（§13.5.1.2）。
    const neo::core::TypeKind baseKind = object->type()->kind();
    if (baseKind == neo::core::TypeKind::Null ||
        baseKind == neo::core::TypeKind::Undefined) {
        return fail(detail::typeError(ctx_, "Cannot convert to object"));
    }
    Value* result = ctx_->deleteProperty(object, key);
    if (detail::isErrorValue(result)) return fail(result);
    // 严格模式：删除失败（不可配置属性）抛 TypeError（§13.5.1.2）。
    if (!ctx_->toBoolean(result) && ctx_->hasFlag("strict")) {
        return fail(detail::typeError(ctx_, "Cannot delete property"));
    }
    push(result);
    return nullptr;
}

Value* VM::opDeleteIdentifier() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("delete_identifier: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("delete_identifier: name index out of range");
    }
    Value* result = ctx_->deleteIdentifier(*name);
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
                                 (flags & kDefineInitialized) != 0,
                                 (flags & kDefineVar) != 0);
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
    // 绑定槽位是活引用（赋值原地 replaceMeta），表达式读取须取快照，
    // 否则值被存入对象/数组后会被后续赋值改写。
    Value* snapshot = ctx_->cloneValue(result);
    if (snapshot == ctx_->oomValue()) return fail(snapshot);
    push(snapshot);
    return nullptr;
}

Value* VM::opLoadSoft() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("load_soft: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("load_soft: name index out of range");
    }

    Value* result = ctx_->loadSoft(*name);
    if (detail::isErrorValue(result)) return fail(result);
    Value* snapshot = ctx_->cloneValue(result);
    if (snapshot == ctx_->oomValue()) return fail(snapshot);
    push(snapshot);
    return nullptr;
}

Value* VM::opDefineIfMissing() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("define_if_missing: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("define_if_missing: name index out of range");
    }
    if (stack_.empty()) {
        return badBytecode("define_if_missing: operand stack underflow");
    }
    Value* value = pop();
    Scope* scope = ctx_->current();
    if (scope == nullptr) return badBytecode("define_if_missing: no scope");
    if (scope->bindingKind(*name) != Scope::BindingKind::None) return nullptr;
    if (!scope->define(*name, value)) {
        return badBytecode("define_if_missing: define failed");
    }
    return nullptr;
}

namespace {
// 完成值寄存器当前是否为 EMPTY（用 Uninitialized 哨兵表示）。
bool isCompletionEmpty(Value* value) {
    return value != nullptr &&
           value->type()->kind() == TypeKind::Uninitialized;
}
}  // namespace

Value* VM::opEnableCompletion() {
    completionMode_ = true;
    return nullptr;
}

Value* VM::opMergeCompletion() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 8) {
        return badBytecode("merge_completion: truncated operands");
    }
    uint32_t completionIndex = readU32(bytes.data() + pc_);
    uint32_t slotIndex = readU32(bytes.data() + pc_ + 4);
    pc_ += 8;

    const std::string* completionName = stringOperand(completionIndex);
    const std::string* slotName = stringOperand(slotIndex);
    if (completionName == nullptr || slotName == nullptr) {
        return badBytecode("merge_completion: name index out of range");
    }
    Value* current = ctx_->load(*completionName);
    if (detail::isErrorValue(current)) return fail(current);
    if (!isCompletionEmpty(current)) return nullptr;
    Value* outer = ctx_->load(*slotName);
    if (detail::isErrorValue(outer)) return fail(outer);
    Value* result = ctx_->store(*completionName, outer);
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opCompletionToUndefined() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("completion_to_undefined: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("completion_to_undefined: name index out of range");
    }
    Value* current = ctx_->load(*name);
    if (detail::isErrorValue(current)) return fail(current);
    if (!isCompletionEmpty(current)) return nullptr;
    Value* result = ctx_->store(*name, ctx_->createUndefined());
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opUpdateLoopValue() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 8) {
        return badBytecode("update_loop_value: truncated operands");
    }
    uint32_t completionIndex = readU32(bytes.data() + pc_);
    uint32_t slotIndex = readU32(bytes.data() + pc_ + 4);
    pc_ += 8;

    const std::string* completionName = stringOperand(completionIndex);
    const std::string* slotName = stringOperand(slotIndex);
    if (completionName == nullptr || slotName == nullptr) {
        return badBytecode("update_loop_value: name index out of range");
    }
    Value* current = ctx_->load(*completionName);
    if (detail::isErrorValue(current)) return fail(current);
    if (isCompletionEmpty(current)) return nullptr;
    Value* result = ctx_->store(*slotName, current);
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opLoopEnd() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 8) {
        return badBytecode("loop_end: truncated operands");
    }
    uint32_t completionIndex = readU32(bytes.data() + pc_);
    uint32_t slotIndex = readU32(bytes.data() + pc_ + 4);
    pc_ += 8;

    const std::string* completionName = stringOperand(completionIndex);
    const std::string* slotName = stringOperand(slotIndex);
    if (completionName == nullptr || slotName == nullptr) {
        return badBytecode("loop_end: name index out of range");
    }
    Value* value = nullptr;
    if (completionMode_ && hasPendingBreak_ && !pendingBreakEmpty_) {
        value = pendingBreakValue_;
    } else {
        value = ctx_->load(*slotName);
        if (detail::isErrorValue(value)) return fail(value);
    }
    hasPendingBreak_ = false;
    Value* result = ctx_->store(*completionName, value);
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opLabelledEnd() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("labelled_end: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("labelled_end: name index out of range");
    }
    if (!completionMode_ || !hasPendingBreak_) return nullptr;
    Value* value = pendingBreakEmpty_ ? ctx_->createUninitialized()
                                      : pendingBreakValue_;
    hasPendingBreak_ = false;
    Value* result = ctx_->store(*name, value);
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opLoadCompletionResult() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("load_completion_result: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("load_completion_result: name index out of range");
    }
    Value* current = ctx_->load(*name);
    if (detail::isErrorValue(current)) return fail(current);
    push(isCompletionEmpty(current) ? ctx_->createUndefined() : current);
    return nullptr;
}

// finally 内执行：当前 try 帧的 abrupt 完成（break/continue）若携带 EMPTY，
// 用槽位值替换，实现 CaseBlockEvaluation 的 UpdateEmpty(R, V)。
Value* VM::opUpdatePendingCompletion() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("update_pending_completion: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode(
            "update_pending_completion: name index out of range");
    }
    if (tryStack_.empty()) return nullptr;
    Value* res = tryStack_.back().res;
    if (res == nullptr || !detail::isInterruptValue(res)) return nullptr;
    auto* data = neo::core::asInterrupt(res);
    if (data == nullptr ||
        (data->kind != neo::core::InterruptKind::Break &&
         data->kind != neo::core::InterruptKind::Continue)) {
        return nullptr;
    }
    if (data->value == nullptr ||
        data->value->type()->kind() != neo::core::TypeKind::Uninitialized) {
        return nullptr;
    }
    Value* value = ctx_->load(*name);
    if (detail::isErrorValue(value)) return fail(value);
    data->value = value;
    return nullptr;
}

Value* VM::opSetFunctionLength() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("set_function_length: truncated operand");
    }
    uint32_t length = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.empty()) {
        return badBytecode("set_function_length: operand stack underflow");
    }
    Value* fn = top();
    Value* value = ctx_->createNumber(static_cast<double>(length));
    if (detail::isErrorValue(value)) return fail(value);
    Value* result = ctx_->defineProperty(fn, "length", value, false, false, true);
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

// 非严格普通函数：Annex B.3.5 风格的自有 caller/arguments（值 null）。
// 严格函数/方法/生成器/箭头不调用本指令，读写走 Function.prototype 受限访问器。
Value* VM::opSetFunctionSloppy() {
    if (stack_.empty()) {
        return badBytecode("set_function_sloppy: operand stack underflow");
    }
    Value* fn = top();
    Value* nullValue = ctx_->createNull();
    if (detail::isErrorValue(nullValue)) return fail(nullValue);
    for (const char* name : {"caller", "arguments"}) {
        Value* result = ctx_->defineProperty(fn, name, nullValue, false, false,
                                             false);
        if (detail::isErrorValue(result)) return fail(result);
    }
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

Value* VM::opStoreVar() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("store_var: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) return badBytecode("store_var: name index out of range");
    if (stack_.empty()) return badBytecode("store_var: operand stack underflow");

    Value* result = ctx_->storeVar(*name, pop());
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opResolveWithRef() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("resolve_with_ref: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("resolve_with_ref: name index out of range");
    }
    // 引用表示为 [base, isWith, hasSlot]：
    //   isWith  → base 是 with 对象（对象环境记录）；
    //   否则 hasSlot → base 是声明式绑定槽位；
    //   都不是 → 运行时按名字重新解析（全局对象/未声明）。
    Value* object = ctx_->resolveWithObject(*name);
    if (object != nullptr && detail::isErrorValue(object)) return fail(object);
    const bool isWith = object != nullptr;
    Value* slot = nullptr;
    if (!isWith) {
        slot = ctx_->resolveBindingSlot(*name);
        if (slot != nullptr && detail::isErrorValue(slot)) return fail(slot);
    }
    const bool hasSlot = slot != nullptr;
    push(isWith ? object
                : (hasSlot ? slot : ctx_->createUndefined()));
    push(ctx_->createBoolean(isWith));
    push(ctx_->createBoolean(hasSlot));
    return nullptr;
}

Value* VM::storeRefValue(Value* base, bool isWith, bool hasSlot,
                         const std::string& name, Value* value) {
    Value* result = nullptr;
    if (isWith) {
        // 对象环境记录 SetMutableBinding（§9.1.1.4.5）步骤 2：写入前先
        // HasProperty。属性在解析后消失时，严格模式抛 ReferenceError，
        // 宽松模式静默忽略（不得在对象上重新创建属性）。
        Value* key = ctx_->createString(name);
        if (key == ctx_->oomValue()) return fail(key);
        Value* has = ctx_->hasProperty(base, key);
        if (detail::isErrorValue(has)) return fail(has);
        if (!detail::toBoolean(has) && ctx_->hasFlag("strict")) {
            return fail(detail::referenceError(
                ctx_, "Assignment to undeclared identifier '" + name + "'"));
        }
        // 宽松模式即使属性已消失仍执行 Set（§9.1.1.4.5 步骤 4）。
        result = ctx_->setProperty(base, key, value);
    } else if (hasSlot) {
        // 声明式绑定：写回 RHS 求值前捕获的槽位，避免 eval 注入更内层
        // 同名 var 后写错目标。
        result = ctx_->storeToSlot(base, name, value);
        if (result == nullptr) result = ctx_->store(name, value);
    } else {
        result = ctx_->store(name, value);
    }
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opStoreWithRef() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("store_with_ref: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("store_with_ref: name index out of range");
    }
    if (stack_.size() < 4) {
        return badBytecode("store_with_ref: operand stack underflow");
    }
    Value* value = pop();
    Value* hasSlot = pop();
    Value* isWith = pop();
    Value* base = pop();
    Value* result = storeRefValue(base, detail::toBoolean(isWith),
                                  detail::toBoolean(hasSlot), *name, value);
    if (result != nullptr) return result;
    push(value);
    return nullptr;
}

Value* VM::opLoadWithRef() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("load_with_ref: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("load_with_ref: name index out of range");
    }
    if (stack_.size() < 3) {
        return badBytecode("load_with_ref: operand stack underflow");
    }
    Value* hasSlot = pop();
    Value* isWith = pop();
    Value* base = pop();
    const bool withRef = detail::toBoolean(isWith);
    const bool slotRef = detail::toBoolean(hasSlot);
    Value* result = nullptr;
    if (withRef) {
        Value* key = ctx_->createString(*name);
        if (key == ctx_->oomValue()) return fail(key);
        // GetBindingValue（§9.1.1.2.6）：引用解析与取值之间属性可能已被
        // 删除（例如 @@unscopables getter）。严格模式抛 ReferenceError，
        // 非严格模式返回 undefined。
        Value* has = ctx_->hasProperty(base, key);
        if (detail::isErrorValue(has)) return fail(has);
        if (!detail::toBoolean(has)) {
            if (ctx_->hasFlag("strict")) {
                return fail(detail::referenceError(
                    ctx_, "'" + *name + "' is not defined"));
            }
            result = ctx_->createUndefined();
        } else {
            result = ctx_->getProperty(base, key);
        }
    } else if (slotRef) {
        result = ctx_->loadFromSlot(base, *name);
        if (result == nullptr) result = ctx_->load(*name);
    } else {
        result = ctx_->load(*name);
    }
    if (detail::isErrorValue(result)) return fail(result);
    push(base);
    push(isWith);
    push(hasSlot);
    push(result);
    return nullptr;
}

Value* VM::opStoreWithRefKeep() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("store_with_ref_keep: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("store_with_ref_keep: name index out of range");
    }
    if (stack_.size() < 5) {
        return badBytecode("store_with_ref_keep: operand stack underflow");
    }
    Value* value = pop();
    Value* old = pop();
    Value* hasSlot = pop();
    Value* isWith = pop();
    Value* base = pop();
    Value* result = storeRefValue(base, detail::toBoolean(isWith),
                                  detail::toBoolean(hasSlot), *name, value);
    if (result != nullptr) return result;
    push(old);
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

Value* VM::opDefineAccessor() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("define_accessor: truncated operand");
    }
    uint32_t flags = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < 4) {
        return badBytecode("define_accessor: operand stack underflow");
    }
    Value* setter = pop();
    Value* getter = pop();
    Value* key = pop();
    Value* object = pop();
    Value* result = ctx_->defineAccessorPartial(
        object, key, getter, (flags & 1u) != 0, setter, (flags & 2u) != 0,
        true, true);
    if (detail::isErrorValue(result)) return fail(result);
    push(object);
    return nullptr;
}

Value* VM::opSetHomeObject() {
    if (stack_.size() < 3) {
        return badBytecode("set_home_object: operand stack underflow");
    }
    Value* fn = stack_.back();
    Value* homeObject = stack_[stack_.size() - 3];
    if (fn->type()->kind() == TypeKind::Function) {
        ctx_->setFunctionHomeObject(fn, homeObject);
    }
    return nullptr;
}

Value* VM::opInheritHomeObject() {
    if (stack_.empty()) {
        return badBytecode("inherit_home_object: operand stack underflow");
    }
    Value* fn = top();
    Value* outer = ctx_->currentFunction();
    if (fn->type()->kind() == TypeKind::Function && outer != nullptr) {
        Value* homeObject = ctx_->functionHomeObject(outer);
        if (homeObject != nullptr) {
            ctx_->setFunctionHomeObject(fn, homeObject);
        }
    }
    return nullptr;
}

Value* VM::opSetProtoLiteral() {
    if (stack_.size() < 2) {
        return badBytecode("set_proto_literal: operand stack underflow");
    }
    Value* proto = pop();
    Value* object = pop();
    // B.3.1：proto 既非 Object 也非 null 时静默忽略。
    if (proto->type()->kind() == TypeKind::Null ||
        proto->type()->hasObjectLayout()) {
        Value* result = ctx_->setPrototype(object, proto);
        if (detail::isErrorValue(result)) return fail(result);
    }
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

Value* VM::opObjectRestDynamic() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("object_rest_dynamic: truncated operand");
    }
    uint32_t count = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < static_cast<size_t>(count) + 2) {
        return badBytecode("object_rest_dynamic: operand stack underflow");
    }
    std::vector<Value*> excluded;
    excluded.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        excluded.push_back(pop());
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

Value* VM::opIteratorNextCached() {
    if (stack_.size() < 2) {
        return badBytecode("iterator_next_cached: operand stack underflow");
    }
    Value* next = pop();
    Value* iterator = pop();
    if (!detail::isCallable(next)) {
        return fail(detail::typeError(ctx_, "iterator next is not a function"));
    }
    Value* result = ctx_->call(next, iterator, 0, nullptr);
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    if (!detail::isObjectKind(result->type()->kind())) {
        return fail(detail::typeError(ctx_, "iterator result is not an object"));
    }
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

Value* VM::opGetAsyncIterator() {
    if (stack_.empty()) {
        return badBytecode("get_async_iterator: operand stack underflow");
    }
    Value* iterable = pop();
    Value* iterator = detail::getAsyncIterator(ctx_, iterable);
    if (detail::isErrorValue(iterator)) return fail(iterator);
    push(iterator);
    return nullptr;
}

Value* VM::opAsyncIteratorClose() {
    if (stack_.empty()) {
        return badBytecode("async_iterator_close: operand stack underflow");
    }
    Value* iterator = pop();
    Value* result = nullptr;
    Value* error = detail::asyncIteratorClose(ctx_, iterator, result);
    if (error != nullptr && detail::isErrorValue(error)) {
        // 已有异常在展开时，close 的错误不覆盖原异常（§7.4.7）。
        if (!tryStack_.empty() && tryStack_.back().res != nullptr &&
            tryStack_.back().res->type()->kind() ==
                neo::core::TypeKind::Exception) {
            push(ctx_->createUndefined());
            return nullptr;
        }
        return fail(error);
    }
    push(result != nullptr ? result : ctx_->createUndefined());
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

// UpdateExpression（§13.4.4/§13.4.5）：ToNumeric(old) 后按同一数值类型 ±1。
Value* VM::increment(bool inc) {
    if (stack_.empty()) {
        return badBytecode("inc/dec: operand stack underflow");
    }
    Value* old = pop();
    Value* numeric = detail::toNumericValue(ctx_, old);
    if (detail::isErrorValue(numeric)) return fail(numeric);
    Value* one = numeric->type()->kind() == TypeKind::BigInt
                    ? ctx_->createBigInt(neo::bigint::BigInt(1))
                    : ctx_->createNumber(1.0);
    if (detail::isErrorValue(one)) return fail(one);
    Value* result = ctx_->binaryOp(inc ? BinaryOperator::Add : BinaryOperator::Sub,
                                   numeric, one);
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opInc() { return increment(true); }
Value* VM::opDec() { return increment(false); }
Value* VM::opMul() { return binary(BinaryOperator::Mul); }
Value* VM::opDiv() { return binary(BinaryOperator::Div); }

Value* VM::opLt() { return binary(BinaryOperator::LessThan); }
Value* VM::opGt() { return binary(BinaryOperator::GreaterThan); }
Value* VM::opLe() { return binary(BinaryOperator::LessThanOrEqual); }
Value* VM::opGe() { return binary(BinaryOperator::GreaterThanOrEqual); }
Value* VM::opStrictEqual() { return binary(BinaryOperator::StrictEqual); }

Value* VM::opIn() {
    if (stack_.size() < 2) {
        return badBytecode("in: operand stack underflow");
    }
    Value* right = pop();
    Value* left = pop();
    Value* result = ctx_->hasProperty(right, left);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opInstanceOf() {
    if (stack_.size() < 2) {
        return badBytecode("instance_of: operand stack underflow");
    }
    Value* right = pop();
    Value* left = pop();
    Value* result = ctx_->instanceOf(left, right);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opBinary() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("binary: truncated operand");
    }
    const uint32_t op = readU32(bytes.data() + pc_);
    pc_ += 4;
    return binary(static_cast<BinaryOperator>(op));
}

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
    frame.normalizeAbruptCompletion = true;
    tryStack_.push_back(frame);
    return nullptr;
}

Value* VM::opTryBeginInternal() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 12) {
        return badBytecode("try_begin_internal: truncated operand");
    }
    TryFrame frame;
    frame.catchStart = readU32(bytes.data() + pc_);
    frame.finallyStart = readU32(bytes.data() + pc_ + 4);
    frame.tryEnd = readU32(bytes.data() + pc_ + 8);
    pc_ += 12;
    frame.scope = ctx_->current();
    frame.stackDepth = stack_.size();
    frame.labelDepth = labelStack_.size();
    frame.normalizeAbruptCompletion = false;
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
    if (bytes.size() - pc_ < 16) {
        return badBytecode("push_label: truncated operand");
    }
    uint32_t labelIndex = readU32(bytes.data() + pc_);
    uint32_t startPc = readU32(bytes.data() + pc_ + 4);
    uint32_t endPc = readU32(bytes.data() + pc_ + 8);
    uint32_t loopValueName = readU32(bytes.data() + pc_ + 12);
    pc_ += 16;

    const std::string* label = stringOperand(labelIndex);
    if (label == nullptr) {
        return badBytecode("push_label: label index out of range");
    }
    labelStack_.push_back(LabelFrame{*label, startPc, endPc, loopValueName,
                                     ctx_->current(), stack_.size(),
                                     tryStack_.size()});
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

Value* VM::opBreakValue() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("break_value: truncated operand");
    }
    uint32_t labelIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.empty()) return badBytecode("break_value: stack underflow");
    Value* value = pop();
    return ctx_->createInterrupt(neo::core::InterruptKind::Break, labelIndex,
                                 value);
}

Value* VM::opContinueValue() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("continue_value: truncated operand");
    }
    uint32_t labelIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.empty()) return badBytecode("continue_value: stack underflow");
    Value* value = pop();
    return ctx_->createInterrupt(neo::core::InterruptKind::Continue, labelIndex,
                                 value);
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
            // 进入 finally 后不得再回到同一 try 的 catch。
            frame.catchStart = 0;
            frame.res = interrupt;
            trackRes(frame, interrupt);
            restoreFrame(frame);
            pc_ = finallyPc;
            return nullptr;
        }
        // try 语句返回 UpdateEmpty(abrupt, undefined)：break/continue 的
        // EMPTY 完成值在跨出 try 时归一化为 undefined。
        if (completionMode_ && frame.normalizeAbruptCompletion &&
            data->value != nullptr &&
            data->value->type()->kind() == neo::core::TypeKind::Uninitialized) {
            data->value = ctx_->createUndefined();
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
        if (completionMode_) {
            hasPendingBreak_ = true;
            pendingBreakEmpty_ =
                data->value != nullptr &&
                data->value->type()->kind() == neo::core::TypeKind::Uninitialized;
            pendingBreakValue_ = data->value;
        }
        labelStack_.resize(targetIndex);
        pc_ = target.endPc;
    } else {
        // §14.7.5.7 step m：continue 的完成值非 EMPTY 时写入目标循环的 V。
        if (completionMode_ && target.loopValueName != UINT32_MAX &&
            data->value != nullptr && !isCompletionEmpty(data->value)) {
            const std::string* slotName = stringOperand(target.loopValueName);
            if (slotName == nullptr) {
                return badBytecode("continue: loop value index out of range");
            }
            Value* stored = ctx_->store(*slotName, data->value);
            if (detail::isErrorValue(stored)) return fail(stored);
        }
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

Value* VM::opPushVarScope() {
    // 函数体开始：形参初始化阶段结束，闭包不再需要按形参环境动态捕获。
    if (Scope* parent = ctx_->current()) parent->clearFlag("param-init");
    Scope* scope = ctx_->pushNewScope(ctx_->current());
    if (scope == nullptr) return ctx_->oomValue();
    scope->setFlag("var-scope");
    return nullptr;
}

Value* VM::opMarkParamInit() {
    if (Scope* scope = ctx_->current()) scope->setFlag("param-init");
    return nullptr;
}

Value* VM::opPopScope() {
    ctx_->popScope();
    return nullptr;
}

Value* VM::opPushWithScope() {
    if (stack_.empty()) {
        return badBytecode("push_with_scope: operand stack underflow");
    }
    Value* object = pop();
    Scope* scope = nullptr;
    Value* result = ctx_->pushWithScope(object, &scope, false);
    if (result != nullptr && detail::isErrorValue(result)) return fail(result);
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
    ctx_->setFunctionArguments(fn, (flags & kFunctionHasArguments) != 0,
                               (flags & kFunctionArgumentsMapped) != 0);
    if ((flags & kFunctionInFieldInitializer) != 0) {
        ctx_->setFunctionInFieldInitializer(fn, true);
    }
    if ((flags & kFunctionArrow) != 0) {
        ctx_->setFunctionArrow(fn, true);
    }
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

Value* VM::opSetParamNames() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("set_param_names: truncated operand");
    }
    uint32_t count = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.empty()) {
        return badBytecode("set_param_names: operand stack underflow");
    }
    std::vector<std::string> names;
    names.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (bytes.size() - pc_ < 4) {
            return badBytecode("set_param_names: truncated name index");
        }
        uint32_t nameIndex = readU32(bytes.data() + pc_);
        pc_ += 4;
        const std::string* name = stringOperand(nameIndex);
        if (name == nullptr) {
            return badBytecode("set_param_names: invalid name index");
        }
        names.push_back(*name);
    }
    ctx_->setFunctionParamNames(top(), names);
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

Value* VM::opSetFuncNameBinding() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("set_func_name_binding: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("set_func_name_binding: name index out of range");
    }
    if (stack_.empty()) {
        return badBytecode("set_func_name_binding: operand stack underflow");
    }

    ctx_->setFunctionNameBinding(top(), *name);
    return nullptr;
}

Value* VM::opSetFuncNameIfAbsent() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("set_func_name_if_absent: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;

    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("set_func_name_if_absent: name index out of range");
    }
    if (stack_.empty()) {
        return badBytecode("set_func_name_if_absent: operand stack underflow");
    }

    ctx_->setFunctionNameIfAbsent(top(), *name);
    return nullptr;
}

Value* VM::opSetFuncNameFromKey() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("set_func_name_from_key: truncated operand");
    }
    uint32_t prefixIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* prefix = stringOperand(prefixIndex);
    if (prefix == nullptr) {
        return badBytecode("set_func_name_from_key: prefix index out of range");
    }
    // 栈：[..., key, fn]；只读不弹。
    if (stack_.size() < 2) {
        return badBytecode("set_func_name_from_key: operand stack underflow");
    }
    ctx_->setFunctionNameFromKey(stack_[stack_.size() - 1],
                                 stack_[stack_.size() - 2], *prefix);
    return nullptr;
}

Value* VM::opThrowReferenceError() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("throw_reference_error: truncated operand");
    }
    uint32_t messageIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* message = stringOperand(messageIndex);
    if (message == nullptr) {
        return badBytecode("throw_reference_error: message index out of range");
    }
    return fail(detail::referenceError(ctx_, *message));
}

Value* VM::opBodyStart() {
    // 参数绑定已完成：把控制权交还调用方，由其保存现场并创建协程。
    return ctx_->createInterrupt(neo::core::InterruptKind::BodyStart, 0,
                                 ctx_->createUndefined());
}

Value* VM::opThrowTypeError() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("throw_type_error: truncated operand");
    }
    uint32_t messageIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* message = stringOperand(messageIndex);
    if (message == nullptr) {
        return badBytecode("throw_type_error: message index out of range");
    }
    return fail(detail::typeError(ctx_, *message));
}

Value* VM::opToPropertyKey() {
    if (stack_.empty()) {
        return badBytecode("to_property_key: operand stack underflow");
    }
    Value* key = pop();
    Value* result = ctx_->toPropertyKey(key);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opToNumeric() {
    if (stack_.empty()) {
        return badBytecode("to_numeric: operand stack underflow");
    }
    Value* value = pop();
    Value* result = detail::toNumericValue(ctx_, value);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opResolveSlot() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("resolve_slot: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("resolve_slot: name index out of range");
    }
    Value* slot = ctx_->resolveBindingSlot(*name);
    if (slot != nullptr && detail::isErrorValue(slot)) return fail(slot);
    const bool found = slot != nullptr;
    push(found ? slot : ctx_->createUndefined());
    push(ctx_->createBoolean(found));
    return nullptr;
}

Value* VM::opLoadSlotRef() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("load_slot_ref: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("load_slot_ref: name index out of range");
    }
    if (stack_.size() < 2) {
        return badBytecode("load_slot_ref: operand stack underflow");
    }
    Value* flag = pop();
    Value* slot = pop();
    Value* result = nullptr;
    if (detail::toBoolean(flag)) {
        result = ctx_->loadFromSlot(slot, *name);
        if (result == nullptr) result = ctx_->load(*name);
    } else {
        result = ctx_->load(*name);
    }
    if (detail::isErrorValue(result)) return fail(result);
    push(slot);
    push(flag);
    push(result);
    return nullptr;
}

Value* VM::opStoreSlotRef() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("store_slot_ref: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("store_slot_ref: name index out of range");
    }
    if (stack_.size() < 3) {
        return badBytecode("store_slot_ref: operand stack underflow");
    }
    Value* value = pop();
    Value* flag = pop();
    Value* slot = pop();
    Value* result = nullptr;
    if (detail::toBoolean(flag)) {
        result = ctx_->storeToSlot(slot, *name, value);
        if (result == nullptr) result = ctx_->store(*name, value);
    } else {
        result = ctx_->store(*name, value);
    }
    if (detail::isErrorValue(result)) return fail(result);
    push(value);
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

Value* VM::opDeclareVar() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("declare_var: truncated operand");
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) return badBytecode("declare_var: name index out of range");

    Value* result = ctx_->declareVar(*name);
    if (result != nullptr && detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opDeclareGlobalVar() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("declare_global_var: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("declare_global_var: name index out of range");
    }
    // 脚本顶层 var：CreateGlobalVarBinding，属性不可配置（§9.1.1.4.17）。
    Value* result = ctx_->declareGlobalVar(*name, false);
    if (result != nullptr && detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opDeclareGlobalFunction() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("declare_global_function: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("declare_global_function: name index out of range");
    }
    // 脚本顶层函数声明：属性不可配置（§9.1.1.4.18）。
    Value* result = ctx_->declareGlobalFunction(*name, false);
    if (result != nullptr && detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opCheckGlobalLexical() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("check_global_lexical: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("check_global_lexical: name index out of range");
    }
    Value* result = ctx_->checkGlobalLexical(*name);
    if (result != nullptr && detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opDefineGlobalLexical() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 8) {
        return badBytecode("define_global_lexical: truncated operand");
    }
    uint32_t nameIndex = readU32(bytes.data() + pc_);
    uint32_t flags = readU32(bytes.data() + pc_ + 4);
    pc_ += 8;
    const std::string* name = stringOperand(nameIndex);
    if (name == nullptr) {
        return badBytecode("define_global_lexical: name index out of range");
    }
    if (stack_.empty()) {
        return badBytecode("define_global_lexical: operand stack underflow");
    }
    Value* result = ctx_->defineGlobalLexical(
        *name, pop(), (flags & kDefineMutable) != 0,
        (flags & kDefineInitialized) != 0);
    if (detail::isErrorValue(result)) return fail(result);
    return nullptr;
}

Value* VM::opDirectEval() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) return badBytecode("direct_eval: truncated operand");
    uint32_t argc = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < static_cast<size_t>(argc) + 1) {
        return badBytecode("direct_eval: operand stack underflow");
    }

    std::vector<Value*> args(argc);
    for (uint32_t i = argc; i > 0; --i) args[i - 1] = pop();
    Value* fn = pop();

    Value* result = nullptr;
    // 按 data 身份判定：表达式读取会 clone 出新的 Value（共享同一 data）。
    if (fn->data() == ctx_->evalFunction()->data()) {
        // 直接 eval：在调用方当前作用域执行（§19.2.1.1）。
        Value* source = argc > 0 ? args[0] : ctx_->createUndefined();
        result = ctx_->evalSource(source, ctx_->current(), true);
    } else {
        // eval 被遮蔽：按普通函数调用处理。
        result = ctx_->call(fn, ctx_->createUndefined(), argc, args.data());
    }
    if (detail::isErrorValue(result)) return fail(result);
    if (detail::isInterruptValue(result)) return result;
    push(result);
    return nullptr;
}

Value* VM::opDirectEvalTail() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("direct_eval_tail: truncated operand");
    }
    uint32_t argc = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < static_cast<size_t>(argc) + 1) {
        return badBytecode("direct_eval_tail: operand stack underflow");
    }

    std::vector<Value*> args(argc);
    for (uint32_t i = argc; i > 0; --i) args[i - 1] = pop();
    Value* fn = pop();

    // 内建 %eval%：直接 eval 在调用方作用域执行，不参与尾调用。
    if (fn->data() == ctx_->evalFunction()->data()) {
        Value* source = argc > 0 ? args[0] : ctx_->createUndefined();
        Value* result = ctx_->evalSource(source, ctx_->current(), true);
        if (detail::isErrorValue(result)) return fail(result);
        if (detail::isInterruptValue(result)) return result;
        push(result);
        return nullptr;
    }
    // eval 被遮蔽：尾位置按尾调用处理（§12.3.4.2 step 7）。
    Value* signal =
        ctx_->createInterrupt(neo::core::InterruptKind::TailCall, pc_, nullptr);
    if (detail::isErrorValue(signal)) return signal;
    auto* data = neo::core::asInterrupt(signal);
    data->tailFn = fn;
    data->tailThis = ctx_->createUndefined();
    data->tailArgs = std::move(args);
    return unwindReturn(signal);
}

Value* VM::opInheritNewTarget() {
    if (stack_.empty()) {
        return badBytecode("inherit_new_target: operand stack underflow");
    }
    Value* fn = stack_.back();
    if (fn->type()->kind() != neo::core::TypeKind::Function) return nullptr;
    auto* d = static_cast<neo::core::FunctionData*>(fn->data());
    if (d->closureScope == nullptr) return nullptr;
    // 箭头函数定义点的外层 new.target；顶层/普通调用为 undefined。
    Value* inherited = ctx_->current() != nullptr
                           ? ctx_->current()->lookupNewTarget()
                           : nullptr;
    d->closureScope->setNewTarget(inherited != nullptr
                                      ? inherited
                                      : ctx_->createUndefined());
    return nullptr;
}

Value* VM::opDirectEvalSpread() {
    if (stack_.size() < 2) {
        return badBytecode("direct_eval_spread: operand stack underflow");
    }
    Value* argsArray = pop();
    Value* fn = pop();
    std::vector<Value*> args;
    if (Value* err = readArrayElements(ctx_, argsArray, args)) return fail(err);

    Value* result = nullptr;
    if (fn->data() == ctx_->evalFunction()->data()) {
        Value* source = args.empty() ? ctx_->createUndefined() : args[0];
        result = ctx_->evalSource(source, ctx_->current(), true);
    } else {
        result = ctx_->call(fn, ctx_->createUndefined(),
                            static_cast<size_t>(args.size()), args.data());
    }
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

Value* VM::opTailCall() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("tail_call: truncated operand");
    }
    uint32_t argc = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < static_cast<size_t>(argc) + 1) {
        return badBytecode("tail_call: operand stack underflow");
    }
    std::vector<Value*> args(argc);
    for (uint32_t i = argc; i > 0; --i) args[i - 1] = pop();
    Value* fn = pop();
    Value* signal =
        ctx_->createInterrupt(neo::core::InterruptKind::TailCall, pc_, nullptr);
    if (detail::isErrorValue(signal)) return signal;
    auto* data = neo::core::asInterrupt(signal);
    data->tailFn = fn;
    data->tailThis = ctx_->createUndefined();
    data->tailArgs = std::move(args);
    // 尾调用等价于 return <call>：先展开本帧剩余的 finally（若有），
    // 展开完毕才把信号交给调用方复用执行帧。
    return unwindReturn(signal);
}

Value* VM::opTailCallMethod() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    if (bytes.size() - pc_ < 4) {
        return badBytecode("tail_call_method: truncated operand");
    }
    uint32_t argc = readU32(bytes.data() + pc_);
    pc_ += 4;
    if (stack_.size() < static_cast<size_t>(argc) + 2) {
        return badBytecode("tail_call_method: operand stack underflow");
    }
    std::vector<Value*> args(argc);
    for (uint32_t i = argc; i > 0; --i) args[i - 1] = pop();
    Value* fn = pop();
    Value* receiver = pop();
    Value* signal =
        ctx_->createInterrupt(neo::core::InterruptKind::TailCall, pc_, nullptr);
    if (detail::isErrorValue(signal)) return signal;
    auto* data = neo::core::asInterrupt(signal);
    data->tailFn = fn;
    data->tailThis = receiver;
    data->tailArgs = std::move(args);
    return unwindReturn(signal);
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

Value* VM::opImportMeta() {
    Value* meta = ctx_->importMeta();
    if (detail::isErrorValue(meta)) return fail(meta);
    push(meta);
    return nullptr;
}

Value* VM::opDynamicImport() {
    if (stack_.empty()) {
        return badBytecode("dynamic_import: operand stack underflow");
    }
    Value* specifier = pop();
    Value* promise = ctx_->importModuleDynamic(specifier, code_->moduleId);
    if (detail::isErrorValue(promise)) return fail(promise);
    push(promise);
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
        // IteratorNext 的结果必须是对象（§7.4.2）。
        if (!detail::isObjectKind(result->type()->kind())) {
            delegateIterator_ = nullptr;
            return fail(detail::typeError(ctx_, "iterator result is not an object"));
        }
        Value* done = ctx_->getProperty(result, "done");
        if (detail::isErrorValue(done)) {
            delegateIterator_ = nullptr;
            return fail(done);
        }
        if (ctx_->toBoolean(done)) {
            delegateIterator_ = nullptr;
            Value* value = ctx_->getProperty(result, "value");
            if (detail::isErrorValue(value)) return fail(value);
            push(value);
            return nullptr;
        }
        // done=false：不读取 value，直接把迭代器结果对象交给 GeneratorYield
        // （§14.4.14 步骤 v），由生成器原样返回给调用方。
        Value* signal = ctx_->createInterrupt(neo::core::InterruptKind::Yield, pc_, result);
        if (detail::isInterruptValue(signal)) {
            neo::core::asInterrupt(signal)->delegateResult = true;
        }
        return signal;
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
        // 注入的 Throw 信号承载 Exception 包装；转发给迭代器 throw 的应是
        // 原始抛出值（§27.5.3.3），否则 Exception 会被当作控制信号传播。
        if (payload != nullptr &&
            payload->type()->kind() == neo::core::TypeKind::Exception) {
            payload = static_cast<neo::core::ExceptionData*>(payload->data())
                          ->child;
        }
        Value* method = ctx_->getProperty(iterator, "throw");
        if (detail::isErrorValue(method)) {
            finish();
            return fail(method);
        }
        if (!detail::isCallable(method)) {
            // 无 throw：先 IteratorClose(iterator, normal)，其错误优先于
            // “缺少 throw 方法”的 TypeError（§14.4.14 步骤 b.iii.2）。
            Value* closeErr = detail::iteratorClose(ctx_, iterator, nullptr);
            finish();
            if (closeErr != nullptr && detail::isErrorValue(closeErr)) {
                return fail(closeErr);
            }
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
        if (!detail::isObjectKind(result->type()->kind())) {
            finish();
            return fail(detail::typeError(ctx_, "iterator result is not an object"));
        }
        Value* done = ctx_->getProperty(result, "done");
        if (detail::isErrorValue(done)) {
            finish();
            return fail(done);
        }
        if (ctx_->toBoolean(done)) {
            Value* value = ctx_->getProperty(result, "value");
            if (detail::isErrorValue(value)) {
                finish();
                return fail(value);
            }
            finish();
            return unwindReturn(value);
        }
        // done=false：不读取 value，原样产出迭代器结果对象
        // （§14.4.14 步骤 c.viii GeneratorYield(innerReturnResult)）。
        Value* signal = ctx_->createInterrupt(neo::core::InterruptKind::Yield, pc_, result);
        if (detail::isInterruptValue(signal)) {
            neo::core::asInterrupt(signal)->delegateResult = true;
        }
        return signal;
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
    // extends 校验（构造器/prototype）在 createScriptClassConstructor 内完成，
    // 保证 Get(superclass, "prototype") 只求值一次。
    Value* ctor = ctx_->createScriptClassConstructor(
        code_, entryPc, paramCount, (flags & kFunctionHasRest) != 0, isDerived,
        superCtor, "");
    if (detail::isErrorValue(ctor)) return fail(ctor);
    ctx_->setFunctionArguments(ctor, (flags & kFunctionHasArguments) != 0,
                               (flags & kFunctionArgumentsMapped) != 0);
    push(ctor);
    return nullptr;
}

Value* VM::opSetStrict() {
    if (stack_.empty()) return badBytecode("set_strict: operand stack underflow");
    ctx_->setFunctionStrict(top());
    return nullptr;
}

Value* VM::opSetScopeStrict() {
    ctx_->current()->setFlag("strict");
    return nullptr;
}

Value* VM::opMarkCatchScope() {
    ctx_->current()->setFlag("catch");
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

Value* VM::opGetSuperBase() {
    Value* fn = ctx_->currentFunction();
    if (fn == nullptr || fn->type()->kind() != TypeKind::Function) {
        return fail(detail::typeError(ctx_, "Cannot read properties of null (reading 'super')"));
    }
    Value* homeObject = ctx_->functionHomeObject(fn);
    if (homeObject == nullptr) {
        return fail(detail::typeError(ctx_, "Cannot read properties of null (reading 'super')"));
    }
    Value* base = ctx_->getPrototype(homeObject);
    if (detail::isErrorValue(base)) return fail(base);
    // GetSuperBase 对 null 原型返回 null 而不抛错；实际的 ToObject 检查由
    // 后续 GetValue/PutValue 完成（§13.15.2 赋值需先求值 RHS）。
    push(base);
    return nullptr;
}

Value* VM::opGetSuperPropFromBase() {
    if (stack_.size() < 2) {
        return badBytecode("get_super_prop_from_base: operand stack underflow");
    }
    Value* key = pop();
    Value* base = pop();
    Value* receiver = ctx_->getThis();
    if (detail::isErrorValue(receiver)) return fail(receiver);
    Value* result = ctx_->getSuperPropertyFromBase(base, key, receiver);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opSetSuperPropFromBase() {
    if (stack_.size() < 3) {
        return badBytecode("set_super_prop_from_base: operand stack underflow");
    }
    Value* value = pop();
    Value* key = pop();
    Value* base = pop();
    Value* receiver = ctx_->getThis();
    if (detail::isErrorValue(receiver)) return fail(receiver);
    Value* result =
        ctx_->setSuperPropertyFromBase(base, key, value, receiver);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opSetSuperPropFromBaseKeep() {
    if (stack_.size() < 4) {
        return badBytecode("set_super_prop_from_base_keep: operand stack underflow");
    }
    Value* keep = pop();
    Value* value = pop();
    Value* key = pop();
    Value* base = pop();
    Value* receiver = ctx_->getThis();
    if (detail::isErrorValue(receiver)) return fail(receiver);
    Value* result =
        ctx_->setSuperPropertyFromBase(base, key, value, receiver);
    if (detail::isErrorValue(result)) return fail(result);
    push(keep);
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

namespace {
// 私有元素操作数：u32 名字下标（不含 '#'）。
bool readPrivateName(VM* /*vm*/, const std::vector<uint8_t>& bytes, uint32_t& pc,
                     uint32_t& index) {
    if (bytes.size() - pc < 4) return false;
    index = readU32(bytes.data() + pc);
    pc += 4;
    return true;
}
}  // namespace

Value* VM::opDefinePrivateField() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("define_private_field: truncated operand");
    }
    if (bytes.size() - pc_ < 4) {
        return badBytecode("define_private_field: truncated operand");
    }
    uint32_t flags = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("define_private_field: string index out of range");
    }
    if (stack_.size() < 2) {
        return badBytecode("define_private_field: operand stack underflow");
    }
    Value* initializer = pop();
    Value* ctor = pop();
    // `#x;`（无初始化器）与 `#x = undefined` 语义相同：槽位初始化为 undefined。
    // 编译器对无初始化器直接压 undefined，这里归一为 nullptr，避免把 undefined
    // 当作可调用初始化器。
    if (initializer != nullptr &&
        initializer->type()->kind() == neo::core::TypeKind::Undefined) {
        initializer = nullptr;
    }
    neo::core::PrivateName* name = ctx_->ensureClassPrivateName(ctor, *desc);
    bool isStatic = (flags & 1u) != 0;
    if (initializer != nullptr &&
        initializer->type()->kind() == neo::core::TypeKind::Function) {
        ctx_->setFunctionOwnerClass(initializer, ctor);
    }
    if (isStatic) {
        ctx_->addStaticPrivateField(ctor, name, initializer);
        Value* result = ctx_->initializeStaticPrivateField(ctor, name);
        ctx_->releasePrivateName(name);
        if (detail::isErrorValue(result)) return fail(result);
    } else {
        ctx_->addPrivateField(ctor, name, initializer);
        ctx_->releasePrivateName(name);
    }
    push(ctor);
    return nullptr;
}

Value* VM::opDefinePrivateMethod() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("define_private_method: truncated operand");
    }
    if (bytes.size() - pc_ < 4) {
        return badBytecode("define_private_method: truncated operand");
    }
    uint32_t flags = readU32(bytes.data() + pc_);
    pc_ += 4;
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("define_private_method: string index out of range");
    }
    if (stack_.size() < 2) {
        return badBytecode("define_private_method: operand stack underflow");
    }
    Value* fn = pop();
    Value* ctor = pop();
    neo::core::PrivateName* name = ctx_->ensureClassPrivateName(ctor, *desc);
    ctx_->definePrivateElement(ctor, name, fn, (flags & 1u) != 0,
                               (flags >> 1) & 3u);
    ctx_->releasePrivateName(name);
    push(ctor);
    return nullptr;
}

namespace {
// 解析当前函数所属 class 的私有名；未声明返回 nullptr。
neo::core::PrivateName* resolvePrivateName(neo::core::IContext* ctx,
                                std::string_view description) {
    return ctx->resolvePrivateName(ctx->currentFunction(), description);
}
}  // namespace

Value* VM::opGetPrivateField() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("get_private_field: truncated operand");
    }
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("get_private_field: string index out of range");
    }
    if (stack_.empty()) {
        return badBytecode("get_private_field: operand stack underflow");
    }
    Value* object = pop();
    neo::core::PrivateName* name = resolvePrivateName(ctx_, *desc);
    if (name == nullptr) {
        return fail(detail::typeError(
            ctx_, "Private member #" + *desc + " is not defined"));
    }
    Value* result = ctx_->getPrivate(object, name);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opGetPrivateMethod() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("get_private_method: truncated operand");
    }
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("get_private_method: string index out of range");
    }
    if (stack_.empty()) {
        return badBytecode("get_private_method: operand stack underflow");
    }
    Value* object = pop();
    neo::core::PrivateName* name = resolvePrivateName(ctx_, *desc);
    if (name == nullptr) {
        return fail(detail::typeError(
            ctx_, "Private member #" + *desc + " is not defined"));
    }
    Value* result =
        ctx_->getPrivateMethod(ctx_->currentFunction(), object, name);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opGetPrivateGetter() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("get_private_getter: truncated operand");
    }
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("get_private_getter: string index out of range");
    }
    if (stack_.empty()) {
        return badBytecode("get_private_getter: operand stack underflow");
    }
    Value* object = pop();
    neo::core::PrivateName* name = resolvePrivateName(ctx_, *desc);
    if (name == nullptr) {
        return fail(detail::typeError(
            ctx_, "Private member #" + *desc + " is not defined"));
    }
    Value* getter =
        ctx_->getPrivateGetter(ctx_->currentFunction(), object, name);
    if (detail::isErrorValue(getter)) return fail(getter);
    Value* result = ctx_->call(getter, object, 0, nullptr);
    if (detail::isErrorValue(result)) return fail(result);
    push(result);
    return nullptr;
}

Value* VM::opSetPrivateField() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("set_private_field: truncated operand");
    }
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("set_private_field: string index out of range");
    }
    if (stack_.size() < 2) {
        return badBytecode("set_private_field: operand stack underflow");
    }
    Value* value = pop();
    Value* object = pop();
    neo::core::PrivateName* name = resolvePrivateName(ctx_, *desc);
    if (name == nullptr) {
        return fail(detail::typeError(
            ctx_, "Private member #" + *desc + " is not defined"));
    }
    Value* result = ctx_->setPrivate(object, name, value);
    if (detail::isErrorValue(result)) return fail(result);
    push(value);
    return nullptr;
}

Value* VM::opSetPrivateSetter() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("set_private_setter: truncated operand");
    }
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("set_private_setter: string index out of range");
    }
    if (stack_.size() < 2) {
        return badBytecode("set_private_setter: operand stack underflow");
    }
    Value* value = pop();
    Value* object = pop();
    neo::core::PrivateName* name = resolvePrivateName(ctx_, *desc);
    if (name == nullptr) {
        return fail(detail::typeError(
            ctx_, "Private member #" + *desc + " is not defined"));
    }
    Value* setter =
        ctx_->getPrivateSetter(ctx_->currentFunction(), object, name);
    if (detail::isErrorValue(setter)) return fail(setter);
    Value* result = ctx_->call(setter, object, 1, &value);
    if (detail::isErrorValue(result)) return fail(result);
    push(value);
    return nullptr;
}

Value* VM::opSetPrivateFieldKeep() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("set_private_field_keep: truncated operand");
    }
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("set_private_field_keep: string index out of range");
    }
    if (stack_.size() < 3) {
        return badBytecode("set_private_field_keep: operand stack underflow");
    }
    Value* keep = pop();
    Value* value = pop();
    Value* object = pop();
    neo::core::PrivateName* name = resolvePrivateName(ctx_, *desc);
    if (name == nullptr) {
        return fail(detail::typeError(
            ctx_, "Private member #" + *desc + " is not defined"));
    }
    Value* result = ctx_->setPrivate(object, name, value);
    if (detail::isErrorValue(result)) return fail(result);
    push(keep);
    return nullptr;
}

Value* VM::opSetPrivateSetterKeep() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("set_private_setter_keep: truncated operand");
    }
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("set_private_setter_keep: string index out of range");
    }
    if (stack_.size() < 3) {
        return badBytecode("set_private_setter_keep: operand stack underflow");
    }
    Value* keep = pop();
    Value* value = pop();
    Value* object = pop();
    neo::core::PrivateName* name = resolvePrivateName(ctx_, *desc);
    if (name == nullptr) {
        return fail(detail::typeError(
            ctx_, "Private member #" + *desc + " is not defined"));
    }
    Value* setter =
        ctx_->getPrivateSetter(ctx_->currentFunction(), object, name);
    if (detail::isErrorValue(setter)) return fail(setter);
    Value* result = ctx_->call(setter, object, 1, &value);
    if (detail::isErrorValue(result)) return fail(result);
    push(keep);
    return nullptr;
}

Value* VM::opSetOwnerClass() {
    if (stack_.size() < 2) {
        return badBytecode("set_owner_class: operand stack underflow");
    }
    Value* ctor = pop();
    Value* fn = pop();
    ctx_->setFunctionOwnerClass(fn, ctor);
    push(fn);
    return nullptr;
}

Value* VM::opPrivateIn() {
    const std::vector<uint8_t>& bytes = code_->bytecode;
    uint32_t nameIndex = 0;
    if (!readPrivateName(this, bytes, pc_, nameIndex)) {
        return badBytecode("private_in: truncated operand");
    }
    const std::string* desc = stringOperand(nameIndex);
    if (desc == nullptr) {
        return badBytecode("private_in: string index out of range");
    }
    if (stack_.empty()) {
        return badBytecode("private_in: operand stack underflow");
    }
    Value* object = pop();
    neo::core::PrivateName* name = resolvePrivateName(ctx_, *desc);
    if (name == nullptr) {
        return fail(detail::typeError(
            ctx_, "Private member #" + *desc + " is not defined"));
    }
    Value* result = ctx_->privateIn(object, name);
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
