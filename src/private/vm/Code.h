// Code.h - 字节码与 SourceMap（ADR-060）
//
// Code 是纯数据：指令数组 + 常量表 + 源码位置表。由 Module / script Function
// 持有，VM 只借用。常量不内联进指令，避免指令变长。
//
// SourceMap 的方向是「源码 → 字节码」：Compiler 为每个 AST 节点记录其生成的
// 指令范围 [pcBegin, pcEnd) 与源码位置。VM 在指令失败时用当前 pc 反查最内层
// 范围，得到该指令对应的 JS 位置（用于补全异常调用栈）。
//
// 关联 ADR: 024（字节码指令集）、037（调用栈）、060（VM 框架）

#ifndef NEO_VM_CODE_H
#define NEO_VM_CODE_H

#include "core/CallStack.h"
#include "vm/OpCode.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace neo::vm {

struct Instruction {
    OpCode op = OpCode::Halt;
    uint32_t a = 0;  // 名字/常量下标、目标 pc、argc 等
    uint32_t b = 0;  // 第二操作数（flags 等）
};

// 一条源码→字节码映射：指令范围 [pcBegin, pcEnd) 对应 position。
struct SourceMapEntry {
    uint32_t pcBegin = 0;
    uint32_t pcEnd = 0;
    neo::core::SourcePosition position;
};

class SourceMap {
public:
    // 登记一个范围。entries_ 保持按 pcBegin 升序（同 pcBegin 保持登记顺序）。
    void add(uint32_t pcBegin, uint32_t pcEnd, neo::core::SourcePosition position) {
        SourceMapEntry entry{pcBegin, pcEnd, std::move(position)};
        auto it = entries_.begin();
        while (it != entries_.end() && it->pcBegin <= pcBegin) ++it;
        entries_.insert(it, std::move(entry));
    }

    // 反查 pc 对应的源码位置：在所有包含 pc 的条目中取范围最窄者
    // （嵌套表达式取最内层）；同宽取 pcBegin 更大者。无命中返回 nullptr。
    const neo::core::SourcePosition* lookup(uint32_t pc) const {
        const SourceMapEntry* best = nullptr;
        uint32_t bestSpan = 0;
        for (const SourceMapEntry& entry : entries_) {
            if (pc < entry.pcBegin || pc >= entry.pcEnd) continue;
            uint32_t span = entry.pcEnd - entry.pcBegin;
            if (best == nullptr || span < bestSpan ||
                (span == bestSpan && entry.pcBegin > best->pcBegin)) {
                best = &entry;
                bestSpan = span;
            }
        }
        return best == nullptr ? nullptr : &best->position;
    }

    const std::vector<SourceMapEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }

private:
    std::vector<SourceMapEntry> entries_;
};

struct Code {
    std::vector<Instruction> instructions;
    std::vector<double> numbers;       // 数值常量表
    std::vector<std::string> strings;  // 字符串/名字表
    SourceMap sourceMap;

    // —— 构造 API（Compiler 与测试共用）——
    // 语义化发射：常量自动进表、指令操作数自动填下标，调用方不直接拼
    // Instruction。所有方法返回该指令的 pc（指令下标）。
    uint32_t emit(OpCode op, uint32_t a = 0, uint32_t b = 0) {
        instructions.push_back(Instruction{op, a, b});
        return static_cast<uint32_t>(instructions.size() - 1);
    }

    // 下一条指令的 pc（用于记录范围/跳转目标）。
    uint32_t here() const { return static_cast<uint32_t>(instructions.size()); }

    uint32_t addNumber(double value) {
        numbers.push_back(value);
        return static_cast<uint32_t>(numbers.size() - 1);
    }

    uint32_t addString(std::string value) {
        strings.push_back(std::move(value));
        return static_cast<uint32_t>(strings.size() - 1);
    }

    uint32_t pushUndefined() { return emit(OpCode::PushUndefined); }
    uint32_t pushNull() { return emit(OpCode::PushNull); }
    uint32_t pushBoolean(bool value) {
        return emit(value ? OpCode::PushTrue : OpCode::PushFalse);
    }
    uint32_t push(double value) { return emit(OpCode::PushNumber, addNumber(value)); }
    uint32_t push(std::string_view value) {
        return emit(OpCode::PushString, addString(std::string(value)));
    }
    // 字面量字符串走精确匹配，避免与 bool/数值重载产生歧义。
    uint32_t push(const char* value) { return push(std::string_view(value)); }

    uint32_t pop() { return emit(OpCode::Pop); }
    uint32_t dup() { return emit(OpCode::Dup); }
    uint32_t swap() { return emit(OpCode::Swap); }

    // 定义绑定：mutableBinding=false 为 const，initialized=false 表示提升建槽
    // 后仍处于 TDZ（后续用 initialize 解除）。
    uint32_t define(std::string_view name, bool mutableBinding = true,
                    bool initialized = true) {
        uint32_t flags = (mutableBinding ? kDefineMutable : 0u) |
                         (initialized ? kDefineInitialized : 0u);
        return emit(OpCode::Define, addString(std::string(name)), flags);
    }
    uint32_t initialize(std::string_view name) {
        return emit(OpCode::Initialize, addString(std::string(name)));
    }
    uint32_t load(std::string_view name) {
        return emit(OpCode::Load, addString(std::string(name)));
    }
    uint32_t store(std::string_view name) {
        return emit(OpCode::Store, addString(std::string(name)));
    }

    uint32_t add() { return emit(OpCode::Add); }
    uint32_t sub() { return emit(OpCode::Sub); }
    uint32_t mul() { return emit(OpCode::Mul); }
    uint32_t div() { return emit(OpCode::Div); }

    uint32_t ret() { return emit(OpCode::Ret); }
    uint32_t halt() { return emit(OpCode::Halt); }

    // 登记源码位置范围（Compiler 发射 AST 节点时调用）。
    void mapSource(uint32_t pcBegin, uint32_t pcEnd,
                   neo::core::SourcePosition position) {
        sourceMap.add(pcBegin, pcEnd, std::move(position));
    }
};

}  // namespace neo::vm

#endif  // NEO_VM_CODE_H
