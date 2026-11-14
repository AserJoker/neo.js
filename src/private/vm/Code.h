// Code.h - 扁平字节码与 SourceMap（ADR-060）
//
// Code 是纯数据：**完全扁平的字节数组** + 常量表 + 源码位置表。pc 是字节偏移，
// 与 CPU 取指流程一致。由 Module / script Function 持有，VM 只借用。
//
// 常量不内联进字节码：push_number/push_string 的操作数是常量表下标。
//
// SourceMap 的方向是「源码 → 字节码」：Compiler 为每个 AST 节点记录其生成的
// 字节范围 [pcBegin, pcEnd) 与源码位置。VM 在指令失败时用当前 pc 反查最内层
// 范围，得到该指令对应的 JS 位置（用于补全异常调用栈）。
//
// 关联 ADR: 024（字节码指令集）、037（调用栈）、060（VM 框架）

#ifndef NEO_VM_CODE_H
#define NEO_VM_CODE_H

#include "core/CallStack.h"
#include "vm/OpCode.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo::vm {

// 一条源码→字节码映射：字节范围 [pcBegin, pcEnd) 对应 position。
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
    // 扁平字节码：[opcode:u16][operand...]，操作数小端。
    std::vector<uint8_t> bytecode;
    std::vector<double> numbers;       // 数值常量表
    std::vector<std::string> strings;  // 字符串/名字表
    SourceMap sourceMap;

    // —— 构造 API（Compiler 与测试共用）——
    // 语义化发射：常量自动进表、操作数按指令格式编码，调用方不直接拼字节。
    // 所有方法返回该指令起始字节偏移（pc）。
    uint32_t here() const { return static_cast<uint32_t>(bytecode.size()); }

    // 发射 opcode（u16 小端），返回其起始偏移；操作数由调用方随后 emitU32。
    uint32_t emitOp(OpCode op) {
        uint32_t pc = here();
        uint16_t value = static_cast<uint16_t>(op);
        bytecode.push_back(static_cast<uint8_t>(value & 0xFFu));
        bytecode.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
        return pc;
    }

    void emitU32(uint32_t value) {
        bytecode.push_back(static_cast<uint8_t>(value & 0xFFu));
        bytecode.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
        bytecode.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
        bytecode.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    }

    uint32_t addNumber(double value) {
        numbers.push_back(value);
        return static_cast<uint32_t>(numbers.size() - 1);
    }

    uint32_t addString(std::string value) {
        strings.push_back(std::move(value));
        return static_cast<uint32_t>(strings.size() - 1);
    }

    uint32_t pushUndefined() { return emitOp(OpCode::PushUndefined); }
    uint32_t pushNull() { return emitOp(OpCode::PushNull); }
    uint32_t pushBoolean(bool value) {
        return emitOp(value ? OpCode::PushTrue : OpCode::PushFalse);
    }
    uint32_t push(double value) {
        uint32_t pc = emitOp(OpCode::PushNumber);
        emitU32(addNumber(value));
        return pc;
    }
    uint32_t push(std::string_view value) {
        uint32_t pc = emitOp(OpCode::PushString);
        emitU32(addString(std::string(value)));
        return pc;
    }
    // 字面量字符串走精确匹配，避免与数值重载产生歧义。
    uint32_t push(const char* value) { return push(std::string_view(value)); }

    uint32_t pop() { return emitOp(OpCode::Pop); }
    uint32_t dup() { return emitOp(OpCode::Dup); }
    uint32_t swap() { return emitOp(OpCode::Swap); }

    // 定义绑定：mutableBinding=false 为 const，initialized=false 表示提升建槽
    // 后仍处于 TDZ（后续用 initialize 解除）。
    uint32_t define(std::string_view name, bool mutableBinding = true,
                    bool initialized = true) {
        uint32_t pc = emitOp(OpCode::Define);
        emitU32(addString(std::string(name)));
        uint32_t flags = (mutableBinding ? kDefineMutable : 0u) |
                         (initialized ? kDefineInitialized : 0u);
        emitU32(flags);
        return pc;
    }
    uint32_t initialize(std::string_view name) {
        uint32_t pc = emitOp(OpCode::Initialize);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t load(std::string_view name) {
        uint32_t pc = emitOp(OpCode::Load);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t store(std::string_view name) {
        uint32_t pc = emitOp(OpCode::Store);
        emitU32(addString(std::string(name)));
        return pc;
    }

    uint32_t add() { return emitOp(OpCode::Add); }
    uint32_t sub() { return emitOp(OpCode::Sub); }
    uint32_t mul() { return emitOp(OpCode::Mul); }
    uint32_t div() { return emitOp(OpCode::Div); }

    uint32_t ret() { return emitOp(OpCode::Ret); }
    uint32_t halt() { return emitOp(OpCode::Halt); }

    // 登记源码位置范围（Compiler 发射 AST 节点时调用）。
    void mapSource(uint32_t pcBegin, uint32_t pcEnd,
                   neo::core::SourcePosition position) {
        sourceMap.add(pcBegin, pcEnd, std::move(position));
    }
};

}  // namespace neo::vm

#endif  // NEO_VM_CODE_H
