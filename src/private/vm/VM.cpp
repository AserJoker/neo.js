// VM.cpp - V0 执行循环（ADR-060）
//
// 只实现 pc + 操作数栈模型：字面量、变量绑定、算术、ret/hlt。
// 所有 JS 语义（转换/运算/绑定规则）都交给 Engine 原语，VM 不重复编排。
//
// 关联 ADR: 004（执行模型）、006（异常即值）、016（运算符原语）、
//          024（字节码指令集）、037（调用栈）、060（VM 框架）

#include "vm/VM.h"

#include "core/ErrorKind.h"
#include "core/IContext.h"
#include "core/OperatorKind.h"
#include "core/Scope.h"
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

const char* opCodeName(OpCode op) {
    switch (op) {
        case OpCode::PushUndefined: return "push_undefined";
        case OpCode::PushNull:      return "push_null";
        case OpCode::PushTrue:      return "push_true";
        case OpCode::PushFalse:     return "push_false";
        case OpCode::PushNumber:    return "push_number";
        case OpCode::PushString:    return "push_string";
        case OpCode::Pop:           return "pop";
        case OpCode::Dup:           return "dup";
        case OpCode::Swap:          return "swap";
        case OpCode::Define:        return "define";
        case OpCode::Initialize:    return "initialize";
        case OpCode::Load:          return "load";
        case OpCode::Store:         return "store";
        case OpCode::Add:           return "add";
        case OpCode::Sub:           return "sub";
        case OpCode::Mul:           return "mul";
        case OpCode::Div:           return "div";
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

SourcePosition VM::currentPosition() const {
    const SourcePosition* position = code_->sourceMap.lookup(pc_);
    return position == nullptr ? SourcePosition{} : *position;
}

const std::string* VM::stringOperand(uint32_t index) const {
    if (index >= code_->strings.size()) return nullptr;
    return &code_->strings[index];
}

Value* VM::fail(Value* error) {
    // Engine 原语创建错误时不知道 pc；VM 用 SourceMap 补全异常栈顶位置。
    ctx_->setExceptionPosition(error, currentPosition());
    return error;
}

Value* VM::badBytecode(const char* message) {
    return fail(ctx_->createErrorException(ErrorKind::Error, message));
}

Value* VM::run(uint32_t entryPc) {
    pc_ = entryPc;
    if (pc_ > code_->instructions.size()) {
        return badBytecode("run: entry pc out of range");
    }
    while (pc_ < code_->instructions.size()) {
        const Instruction instruction = code_->instructions[pc_];

        switch (instruction.op) {
            case OpCode::PushUndefined:
                push(ctx_->createUndefined());
                break;
            case OpCode::PushNull:
                push(ctx_->createNull());
                break;
            case OpCode::PushTrue:
                push(ctx_->createBoolean(true));
                break;
            case OpCode::PushFalse:
                push(ctx_->createBoolean(false));
                break;
            case OpCode::PushNumber:
                if (instruction.a >= code_->numbers.size()) {
                    return badBytecode("push_number: constant index out of range");
                }
                push(ctx_->createNumber(code_->numbers[instruction.a]));
                break;
            case OpCode::PushString: {
                const std::string* value = stringOperand(instruction.a);
                if (value == nullptr) {
                    return badBytecode("push_string: constant index out of range");
                }
                push(ctx_->createString(*value));
                break;
            }
            case OpCode::Pop:
                if (stack_.empty()) return badBytecode("pop: operand stack underflow");
                pop();
                break;
            case OpCode::Dup:
                if (stack_.empty()) return badBytecode("dup: operand stack underflow");
                push(top());
                break;
            case OpCode::Swap:
                if (stack_.size() < 2) {
                    return badBytecode("swap: operand stack underflow");
                }
                std::swap(stack_[stack_.size() - 1], stack_[stack_.size() - 2]);
                break;
            case OpCode::Define: {
                const std::string* name = stringOperand(instruction.a);
                if (name == nullptr) {
                    return badBytecode("define: name index out of range");
                }
                if (stack_.empty()) {
                    return badBytecode("define: operand stack underflow");
                }
                Value* value = pop();
                Value* result = ctx_->define(
                    *name, value, (instruction.b & kDefineMutable) != 0,
                    (instruction.b & kDefineInitialized) != 0);
                if (detail::isErrorValue(result)) return fail(result);
                break;
            }
            case OpCode::Initialize: {
                const std::string* name = stringOperand(instruction.a);
                if (name == nullptr) {
                    return badBytecode("initialize: name index out of range");
                }
                if (stack_.empty()) {
                    return badBytecode("initialize: operand stack underflow");
                }
                Value* result = ctx_->initialize(*name, pop());
                if (detail::isErrorValue(result)) return fail(result);
                break;
            }
            case OpCode::Load: {
                const std::string* name = stringOperand(instruction.a);
                if (name == nullptr) {
                    return badBytecode("load: name index out of range");
                }
                Value* result = ctx_->load(*name);
                if (detail::isErrorValue(result)) return fail(result);
                push(result);
                break;
            }
            case OpCode::Store: {
                const std::string* name = stringOperand(instruction.a);
                if (name == nullptr) {
                    return badBytecode("store: name index out of range");
                }
                if (stack_.empty()) {
                    return badBytecode("store: operand stack underflow");
                }
                Value* result = ctx_->store(*name, pop());
                if (detail::isErrorValue(result)) return fail(result);
                break;
            }
            case OpCode::Add:
            case OpCode::Sub:
            case OpCode::Mul:
            case OpCode::Div: {
                if (stack_.size() < 2) {
                    return badBytecode("binary op: operand stack underflow");
                }
                Value* right = pop();
                Value* left = pop();
                BinaryOperator op = BinaryOperator::Add;
                switch (instruction.op) {
                    case OpCode::Add: op = BinaryOperator::Add; break;
                    case OpCode::Sub: op = BinaryOperator::Sub; break;
                    case OpCode::Mul: op = BinaryOperator::Mul; break;
                    default:          op = BinaryOperator::Div; break;
                }
                Value* result = ctx_->binaryOp(op, left, right);
                if (detail::isErrorValue(result)) return fail(result);
                // Interrupt 是控制流信号（协程/异步挂起），原样交给外部消费。
                if (detail::isInterruptValue(result)) return result;
                push(result);
                break;
            }
            case OpCode::Ret:
                return stack_.empty() ? ctx_->createUndefined() : pop();
            case OpCode::Halt:
                return ctx_->createUndefined();
        }

        ++pc_;
    }
    // 指令流自然结束：等价于 hlt。
    return ctx_->createUndefined();
}

}  // namespace neo::vm
