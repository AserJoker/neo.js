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
};

}  // namespace neo::vm

#endif  // NEO_VM_CODE_H
