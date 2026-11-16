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

namespace neo::core {
class Value;
}  // namespace neo::core

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
    // 标签模板对象缓存（§13.2.8.2）：pc → 模板对象。同一 Code 的同一调用点
    // 复用同一对象；Value 由 Context 挂到 global scope 作 GC 根。
    mutable std::vector<std::pair<uint32_t, neo::core::Value*>> templateCache;

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

    // 回填已发射的 u32 操作数（前向跳转/标签解析用）。at 为操作数字节偏移。
    void patchU32(uint32_t at, uint32_t value) {
        bytecode[at] = static_cast<uint8_t>(value & 0xFFu);
        bytecode[at + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        bytecode[at + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        bytecode[at + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
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
    uint32_t pushUninitialized() { return emitOp(OpCode::PushUninitialized); }
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
    uint32_t clone() { return emitOp(OpCode::Clone); }
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

    uint32_t getProp() { return emitOp(OpCode::GetProp); }
    uint32_t setProp() { return emitOp(OpCode::SetProp); }
    uint32_t newObject() { return emitOp(OpCode::NewObject); }
    uint32_t newArray(uint32_t length = 0) {
        uint32_t pc = emitOp(OpCode::NewArray);
        emitU32(length);
        return pc;
    }
    uint32_t defineProp() { return emitOp(OpCode::DefineProp); }
    uint32_t arrayAppend() { return emitOp(OpCode::ArrayAppend); }
    uint32_t arrayGrow() { return emitOp(OpCode::ArrayGrow); }
    uint32_t arraySpread() { return emitOp(OpCode::ArraySpread); }
    uint32_t objectSpread() { return emitOp(OpCode::ObjectSpread); }
    uint32_t objectRest(const std::vector<std::string>& excluded) {
        uint32_t pc = emitOp(OpCode::ObjectRest);
        emitU32(static_cast<uint32_t>(excluded.size()));
        for (const std::string& key : excluded) {
            emitU32(addString(key));
        }
        return pc;
    }
    uint32_t forInKeys() { return emitOp(OpCode::ForInKeys); }
    uint32_t isEnumerableOwn() { return emitOp(OpCode::IsEnumerableOwn); }
    uint32_t getIterator() { return emitOp(OpCode::GetIterator); }
    uint32_t iteratorNext() { return emitOp(OpCode::IteratorNext); }
    uint32_t iteratorClose() { return emitOp(OpCode::IteratorClose); }

    uint32_t add() { return emitOp(OpCode::Add); }
    uint32_t sub() { return emitOp(OpCode::Sub); }
    uint32_t mul() { return emitOp(OpCode::Mul); }
    uint32_t div() { return emitOp(OpCode::Div); }

    // 一元运算：op 为 core::UnaryOperator 的数值。
    uint32_t unary(uint32_t op) {
        uint32_t pc = emitOp(OpCode::Unary);
        emitU32(op);
        return pc;
    }

    uint32_t toString() { return emitOp(OpCode::ToString); }

    // 标签模板对象：quasis 为 (cooked, raw) 字符串对。
    uint32_t templateObject(
        const std::vector<std::pair<std::string, std::string>>& quasis) {
        uint32_t pc = emitOp(OpCode::TemplateObject);
        emitU32(static_cast<uint32_t>(quasis.size()));
        for (const auto& quasi : quasis) {
            emitU32(addString(quasi.first));
            emitU32(addString(quasi.second));
        }
        return pc;
    }

    uint32_t lt() { return emitOp(OpCode::Lt); }
    uint32_t gt() { return emitOp(OpCode::Gt); }
    uint32_t le() { return emitOp(OpCode::Le); }
    uint32_t ge() { return emitOp(OpCode::Ge); }
    uint32_t strictEqual() { return emitOp(OpCode::StrictEqual); }

    // 跳转：target 为字节偏移。前向跳转先以 0 占位，稍后 patchU32(pc + 2, target)。
    uint32_t jmp(uint32_t target = 0) {
        uint32_t pc = emitOp(OpCode::Jmp);
        emitU32(target);
        return pc;
    }
    uint32_t jtrue(uint32_t target = 0) {
        uint32_t pc = emitOp(OpCode::JTrue);
        emitU32(target);
        return pc;
    }
    uint32_t jfalse(uint32_t target = 0) {
        uint32_t pc = emitOp(OpCode::JFalse);
        emitU32(target);
        return pc;
    }
    uint32_t jNotNullish(uint32_t target = 0) {
        uint32_t pc = emitOp(OpCode::JNotNullish);
        emitU32(target);
        return pc;
    }

    // try：catch/finally 为 out-of-line 块，tryEnd 为整条语句之后的位置。
    uint32_t tryBegin(uint32_t catchStart = 0, uint32_t finallyStart = 0,
                      uint32_t tryEnd = 0) {
        uint32_t pc = emitOp(OpCode::TryBegin);
        emitU32(catchStart);
        emitU32(finallyStart);
        emitU32(tryEnd);
        return pc;
    }
    uint32_t tryBodyEnd() { return emitOp(OpCode::TryBodyEnd); }
    uint32_t catchEnd() { return emitOp(OpCode::CatchEnd); }
    uint32_t finallyEnd() { return emitOp(OpCode::FinallyEnd); }
    uint32_t throwValue() { return emitOp(OpCode::Throw); }

    // label 栈：startPc = continue 目标，endPc = break 目标（均为 0 表示无）。
    uint32_t pushLabel(std::string_view label, uint32_t startPc = 0,
                       uint32_t endPc = 0) {
        uint32_t pc = emitOp(OpCode::PushLabel);
        emitU32(addString(std::string(label)));
        emitU32(startPc);
        emitU32(endPc);
        return pc;
    }
    uint32_t popLabel(std::string_view label) {
        uint32_t pc = emitOp(OpCode::PopLabel);
        emitU32(addString(std::string(label)));
        return pc;
    }
    uint32_t popLabelIfTop(std::string_view label) {
        uint32_t pc = emitOp(OpCode::PopLabelIfTop);
        emitU32(addString(std::string(label)));
        return pc;
    }
    uint32_t breakTo(std::string_view label) {
        uint32_t pc = emitOp(OpCode::Break);
        emitU32(addString(std::string(label)));
        return pc;
    }
    uint32_t continueTo(std::string_view label) {
        uint32_t pc = emitOp(OpCode::Continue);
        emitU32(addString(std::string(label)));
        return pc;
    }

    uint32_t pushScope() { return emitOp(OpCode::PushScope); }
    uint32_t popScope() { return emitOp(OpCode::PopScope); }

    // 逐迭代环境：u32 count + count × u32 nameIndex。
    uint32_t iterationEnv(const std::vector<std::string>& names) {
        uint32_t pc = emitOp(OpCode::IterationEnv);
        emitU32(static_cast<uint32_t>(names.size()));
        for (const std::string& name : names) emitU32(addString(name));
        return pc;
    }
    uint32_t commitIterationEnv(const std::vector<std::string>& names) {
        uint32_t pc = emitOp(OpCode::CommitIterationEnv);
        emitU32(static_cast<uint32_t>(names.size()));
        for (const std::string& name : names) emitU32(addString(name));
        return pc;
    }

    // —— 函数定义 ——
    uint32_t pushFunction(uint32_t entryPc, uint32_t paramCount,
                          uint32_t flags = kFunctionConstructible |
                                           kFunctionHasPrototype) {
        uint32_t pc = emitOp(OpCode::PushFunction);
        emitU32(entryPc);
        emitU32(paramCount);
        emitU32(flags);
        return pc;
    }
    uint32_t setClosure(std::string_view name) {
        uint32_t pc = emitOp(OpCode::SetClosure);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t setFuncName(std::string_view name) {
        uint32_t pc = emitOp(OpCode::SetFuncName);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t bindThis() { return emitOp(OpCode::BindThis); }
    uint32_t loadThis() { return emitOp(OpCode::LoadThis); }
    uint32_t storeThis() { return emitOp(OpCode::StoreThis); }

    // —— 调用 ——
    uint32_t call(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::Call);
        emitU32(argc);
        return pc;
    }
    uint32_t callMethod(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::CallMethod);
        emitU32(argc);
        return pc;
    }
    uint32_t callSpread() { return emitOp(OpCode::CallSpread); }
    uint32_t callMethodSpread() { return emitOp(OpCode::CallMethodSpread); }
    uint32_t construct(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::New);
        emitU32(argc);
        return pc;
    }
    uint32_t constructSpread() { return emitOp(OpCode::NewSpread); }
    uint32_t loadNewTarget() { return emitOp(OpCode::LoadNewTarget); }

    uint32_t yieldValue() { return emitOp(OpCode::Yield); }
    uint32_t awaitValue() { return emitOp(OpCode::Await); }
    uint32_t ret() { return emitOp(OpCode::Ret); }
    uint32_t halt() { return emitOp(OpCode::Halt); }
    uint32_t restArgs() { return emitOp(OpCode::RestArgs); }

    // —— class ——
    uint32_t pushClass(uint32_t entryPc, uint32_t paramCount, uint32_t flags) {
        uint32_t pc = emitOp(OpCode::PushClass);
        emitU32(entryPc);
        emitU32(paramCount);
        emitU32(flags);
        return pc;
    }
    uint32_t setStrict() { return emitOp(OpCode::SetStrict); }
    uint32_t initInstanceElements() {
        return emitOp(OpCode::InitInstanceElements);
    }
    uint32_t constructSuper(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::ConstructSuper);
        emitU32(argc);
        return pc;
    }
    uint32_t constructSuperSpread() {
        return emitOp(OpCode::ConstructSuperSpread);
    }
    uint32_t getSuperProp() { return emitOp(OpCode::GetSuperProp); }
    uint32_t setSuperProp() { return emitOp(OpCode::SetSuperProp); }
    uint32_t defineClassMethod(uint32_t flags) {
        uint32_t pc = emitOp(OpCode::DefineClassMethod);
        emitU32(flags);
        return pc;
    }
    uint32_t defineClassField(uint32_t flags) {
        uint32_t pc = emitOp(OpCode::DefineClassField);
        emitU32(flags);
        return pc;
    }

    // 登记源码位置范围（Compiler 发射 AST 节点时调用）。
    void mapSource(uint32_t pcBegin, uint32_t pcEnd,
                   neo::core::SourcePosition position) {
        sourceMap.add(pcBegin, pcEnd, std::move(position));
    }
};

}  // namespace neo::vm

#endif  // NEO_VM_CODE_H
