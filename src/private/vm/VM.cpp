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
        case OpCode::Lt:            return "lt";
        case OpCode::Gt:            return "gt";
        case OpCode::Le:            return "le";
        case OpCode::Ge:            return "ge";
        case OpCode::Jmp:           return "jmp";
        case OpCode::JTrue:         return "jtrue";
        case OpCode::JFalse:        return "jfalse";
        case OpCode::PushScope:     return "push_scope";
        case OpCode::PopScope:      return "pop_scope";
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
        &VM::opPushNull,
        &VM::opPushTrue,
        &VM::opPushFalse,
        &VM::opPushNumber,
        &VM::opPushString,
        &VM::opPop,
        &VM::opDup,
        &VM::opSwap,
        &VM::opDefine,
        &VM::opInitialize,
        &VM::opLoad,
        &VM::opStore,
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
        &VM::opPushScope,
        &VM::opPopScope,
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

// —— 作用域 ——

Value* VM::opPushScope() {
    if (ctx_->pushNewScope(ctx_->current()) == nullptr) return ctx_->oomValue();
    return nullptr;
}

Value* VM::opPopScope() {
    ctx_->popScope();
    return nullptr;
}

// —— 结束 ——

Value* VM::opRet() {
    return stack_.empty() ? ctx_->createUndefined() : pop();
}

Value* VM::opHalt() {
    return ctx_->createUndefined();
}

}  // namespace neo::vm
