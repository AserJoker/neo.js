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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neo::core {
class Value;
}  // namespace neo::core

namespace neo::regex {
struct Program;
}  // namespace neo::regex

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
    // 字符串常量去重：value → strings 下标。
    std::unordered_map<std::string, uint32_t> stringIndex;
    // 正则字面量的编译产物（Compiler 预编译；VM 直接复用，避免运行期重编译）。
    // 与 pattern/flags 字符串常量分开存放，Program 是不可变纯 C++ 数据。
    std::vector<std::shared_ptr<const neo::regex::Program>> regexPrograms;
    SourceMap sourceMap;
    // 本 Code 所属模块 id（动态 import 相对说明符的基准）；script 为文件名。
    std::string moduleId;
    // 脚本程序（compileProgram）：VM 入口为其建立临时执行作用域，
    // strict 决定该作用域是否标注 "strict"（脚本默认 sloppy）。
    bool isScriptProgram = false;
    bool strict = false;
    // [[HasTLA]]：模块顶层含 await / for await（不含嵌套函数）。
    // 异步模块求值在 SCC 展开前需要静态知道这一点（ADR-064）。
    bool hasTopLevelAwait = false;
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
        auto it = stringIndex.find(value);
        if (it != stringIndex.end()) return it->second;
        const uint32_t index = static_cast<uint32_t>(strings.size());
        stringIndex.emplace(value, index);
        strings.push_back(std::move(value));
        return index;
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
    // BigInt 字面量：digits 不含前缀，radix 为 2/8/10/16。
    uint32_t pushBigInt(std::string_view digits, uint32_t radix) {
        uint32_t pc = emitOp(OpCode::PushBigInt);
        emitU32(addString(std::string(digits)));
        emitU32(radix);
        return pc;
    }
    // 正则字面量：pattern 与 flags 分别进字符串常量表；program 为 Compiler
    // 预编译产物（可空，空时 VM 回退到 createRegExp 的运行期编译）。
    static constexpr uint32_t kNoRegexProgram = 0xFFFFFFFFu;
    uint32_t pushRegExp(std::string_view pattern, std::string_view flags,
                        std::shared_ptr<const neo::regex::Program> program =
                            nullptr) {
        uint32_t pc = emitOp(OpCode::PushRegExp);
        emitU32(addString(std::string(pattern)));
        emitU32(addString(std::string(flags)));
        uint32_t programIndex = kNoRegexProgram;
        if (program != nullptr) {
            programIndex = static_cast<uint32_t>(regexPrograms.size());
            regexPrograms.push_back(std::move(program));
        }
        emitU32(programIndex);
        return pc;
    }
    // 字面量字符串走精确匹配，避免与数值重载产生歧义。
    uint32_t push(const char* value) { return push(std::string_view(value)); }

    uint32_t pop() { return emitOp(OpCode::Pop); }
    uint32_t dup() { return emitOp(OpCode::Dup); }
    uint32_t clone() { return emitOp(OpCode::Clone); }
    uint32_t swap() { return emitOp(OpCode::Swap); }
    uint32_t dup2() { return emitOp(OpCode::Dup2); }

    // 定义绑定：mutableBinding=false 为 const，initialized=false 表示提升建槽
    // 后仍处于 TDZ（后续用 initialize 解除）。
    uint32_t define(std::string_view name, bool mutableBinding = true,
                    bool initialized = true, bool varBinding = false) {
        uint32_t pc = emitOp(OpCode::Define);
        emitU32(addString(std::string(name)));
        uint32_t flags = (mutableBinding ? kDefineMutable : 0u) |
                         (initialized ? kDefineInitialized : 0u) |
                         (varBinding ? kDefineVar : 0u);
        emitU32(flags);
        return pc;
    }
    // eval 变量声明：只在变量环境缺失时建 undefined 槽，已有绑定保持原值。
    uint32_t declareVar(std::string_view name) {
        uint32_t pc = emitOp(OpCode::DeclareVar);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t declareGlobalVar(std::string_view name) {
        uint32_t pc = emitOp(OpCode::DeclareGlobalVar);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t declareGlobalFunction(std::string_view name) {
        uint32_t pc = emitOp(OpCode::DeclareGlobalFunction);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t checkGlobalLexical(std::string_view name) {
        uint32_t pc = emitOp(OpCode::CheckGlobalLexical);
        emitU32(addString(std::string(name)));
        return pc;
    }
    // 全局声明记录（GlobalScope）定义 let/const/class，flags 与 define 相同。
    uint32_t defineGlobalLexical(std::string_view name, bool mutableBinding,
                                 bool initialized) {
        uint32_t pc = emitOp(OpCode::DefineGlobalLexical);
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
    uint32_t loadSoft(std::string_view name) {
        uint32_t pc = emitOp(OpCode::LoadSoft);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t defineIfMissing(std::string_view name) {
        uint32_t pc = emitOp(OpCode::DefineIfMissing);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t enableCompletion() { return emitOp(OpCode::EnableCompletion); }
    uint32_t mergeCompletion(std::string_view completionName,
                             std::string_view slotName) {
        uint32_t pc = emitOp(OpCode::MergeCompletion);
        emitU32(addString(std::string(completionName)));
        emitU32(addString(std::string(slotName)));
        return pc;
    }
    uint32_t completionToUndefined(std::string_view completionName) {
        uint32_t pc = emitOp(OpCode::CompletionToUndefined);
        emitU32(addString(std::string(completionName)));
        return pc;
    }
    uint32_t updateLoopValue(std::string_view completionName,
                             std::string_view loopSlot) {
        uint32_t pc = emitOp(OpCode::UpdateLoopValue);
        emitU32(addString(std::string(completionName)));
        emitU32(addString(std::string(loopSlot)));
        return pc;
    }
    uint32_t loopEnd(std::string_view completionName, std::string_view loopSlot) {
        uint32_t pc = emitOp(OpCode::LoopEnd);
        emitU32(addString(std::string(completionName)));
        emitU32(addString(std::string(loopSlot)));
        return pc;
    }
    uint32_t labelledEnd(std::string_view completionName) {
        uint32_t pc = emitOp(OpCode::LabelledEnd);
        emitU32(addString(std::string(completionName)));
        return pc;
    }
    uint32_t setFunctionSloppy() { return emitOp(OpCode::SetFunctionSloppy); }
    uint32_t setFunctionLength(uint32_t length) {
        uint32_t pc = emitOp(OpCode::SetFunctionLength);
        emitU32(length);
        return pc;
    }
    uint32_t updatePendingCompletion(std::string_view slotName) {
        uint32_t pc = emitOp(OpCode::UpdatePendingCompletion);
        emitU32(addString(std::string(slotName)));
        return pc;
    }
    uint32_t loadCompletionResult(std::string_view completionName) {
        uint32_t pc = emitOp(OpCode::LoadCompletionResult);
        emitU32(addString(std::string(completionName)));
        return pc;
    }
    uint32_t store(std::string_view name) {
        uint32_t pc = emitOp(OpCode::Store);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t storeVar(std::string_view name) {
        uint32_t pc = emitOp(OpCode::StoreVar);
        emitU32(addString(std::string(name)));
        return pc;
    }

    uint32_t getProp() { return emitOp(OpCode::GetProp); }
    uint32_t setProp() { return emitOp(OpCode::SetProp); }
    uint32_t deleteProp() { return emitOp(OpCode::DeleteProp); }
    uint32_t deleteIdentifier(std::string_view name) {
        uint32_t pc = emitOp(OpCode::DeleteIdentifier);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t setPropKeep() { return emitOp(OpCode::SetPropKeep); }
    uint32_t newObject() { return emitOp(OpCode::NewObject); }
    uint32_t newArray(uint32_t length = 0) {
        uint32_t pc = emitOp(OpCode::NewArray);
        emitU32(length);
        return pc;
    }
    uint32_t defineProp() { return emitOp(OpCode::DefineProp); }
    uint32_t resolveWithRef(std::string_view name) {
        uint32_t pc = emitOp(OpCode::ResolveWithRef);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t storeWithRef(std::string_view name) {
        uint32_t pc = emitOp(OpCode::StoreWithRef);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t loadWithRef(std::string_view name) {
        uint32_t pc = emitOp(OpCode::LoadWithRef);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t storeWithRefKeep(std::string_view name) {
        uint32_t pc = emitOp(OpCode::StoreWithRefKeep);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t resolveSlot(std::string_view name) {
        uint32_t pc = emitOp(OpCode::ResolveSlot);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t loadSlotRef(std::string_view name) {
        uint32_t pc = emitOp(OpCode::LoadSlotRef);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t storeSlotRef(std::string_view name) {
        uint32_t pc = emitOp(OpCode::StoreSlotRef);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t markParamInit() { return emitOp(OpCode::MarkParamInit); }
    uint32_t getSuperBase() { return emitOp(OpCode::GetSuperBase); }
    uint32_t getSuperPropFromBase() {
        return emitOp(OpCode::GetSuperPropFromBase);
    }
    uint32_t setSuperPropFromBase() {
        return emitOp(OpCode::SetSuperPropFromBase);
    }
    uint32_t setSuperPropFromBaseKeep() {
        return emitOp(OpCode::SetSuperPropFromBaseKeep);
    }
    uint32_t setHomeObject() { return emitOp(OpCode::SetHomeObject); }
    uint32_t inheritHomeObject() { return emitOp(OpCode::InheritHomeObject); }
    uint32_t setProtoLiteral() { return emitOp(OpCode::SetProtoLiteral); }
    uint32_t defineAccessor(bool hasGetter, bool hasSetter) {
        uint32_t pc = emitOp(OpCode::DefineAccessor);
        emitU32((hasGetter ? 1u : 0u) | (hasSetter ? 2u : 0u));
        return pc;
    }
    uint32_t arrayAppend() { return emitOp(OpCode::ArrayAppend); }
    uint32_t arrayGrow() { return emitOp(OpCode::ArrayGrow); }
    uint32_t arraySpread() { return emitOp(OpCode::ArraySpread); }
    uint32_t objectSpread() { return emitOp(OpCode::ObjectSpread); }
    uint32_t objectRestDynamic(uint32_t count) {
        uint32_t pc = emitOp(OpCode::ObjectRestDynamic);
        emitU32(count);
        return pc;
    }
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
    uint32_t iteratorNextCached() {
        return emitOp(OpCode::IteratorNextCached);
    }
    uint32_t iteratorClose() { return emitOp(OpCode::IteratorClose); }
    uint32_t getAsyncIterator() { return emitOp(OpCode::GetAsyncIterator); }
    uint32_t asyncIteratorClose() {
        return emitOp(OpCode::AsyncIteratorClose);
    }

    uint32_t add() { return emitOp(OpCode::Add); }
    uint32_t sub() { return emitOp(OpCode::Sub); }
    uint32_t mul() { return emitOp(OpCode::Mul); }
    uint32_t div() { return emitOp(OpCode::Div); }
    uint32_t inc() { return emitOp(OpCode::Inc); }
    uint32_t dec() { return emitOp(OpCode::Dec); }

    // 一元运算：op 为 core::UnaryOperator 的数值。
    uint32_t unary(uint32_t op) {
        uint32_t pc = emitOp(OpCode::Unary);
        emitU32(op);
        return pc;
    }

    uint32_t toString() { return emitOp(OpCode::ToString); }

    // 标签模板对象：quasis 为 (cooked, raw) 字符串对。
    // cooked 为空表示非法转义（cooked=undefined），用 0xFFFFFFFF 哨兵编码。
    static constexpr uint32_t kUndefinedString = 0xFFFFFFFFu;
    uint32_t templateObject(
        const std::vector<std::pair<std::optional<std::string>, std::string>>&
            quasis) {
        uint32_t pc = emitOp(OpCode::TemplateObject);
        emitU32(static_cast<uint32_t>(quasis.size()));
        for (const auto& quasi : quasis) {
            emitU32(quasi.first ? addString(*quasi.first) : kUndefinedString);
            emitU32(addString(quasi.second));
        }
        return pc;
    }

    uint32_t lt() { return emitOp(OpCode::Lt); }
    uint32_t gt() { return emitOp(OpCode::Gt); }
    uint32_t le() { return emitOp(OpCode::Le); }
    uint32_t ge() { return emitOp(OpCode::Ge); }
    uint32_t strictEqual() { return emitOp(OpCode::StrictEqual); }
    uint32_t in() { return emitOp(OpCode::In); }
    uint32_t instanceOf() { return emitOp(OpCode::InstanceOf); }
    // 通用二元运算：操作数为 core::BinaryOperator 的数值。
    uint32_t binary(uint32_t op) {
        uint32_t pc = emitOp(OpCode::Binary);
        emitU32(op);
        return pc;
    }

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
    uint32_t tryBeginInternal(uint32_t catchStart = 0,
                              uint32_t finallyStart = 0,
                              uint32_t tryEnd = 0) {
        uint32_t pc = emitOp(OpCode::TryBeginInternal);
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
    // loopValueName 为循环 V 槽的字符串下标（UINT32_MAX = 无）。
    uint32_t pushLabel(std::string_view label, uint32_t startPc = 0,
                       uint32_t endPc = 0,
                       uint32_t loopValueName = UINT32_MAX) {
        uint32_t pc = emitOp(OpCode::PushLabel);
        emitU32(addString(std::string(label)));
        emitU32(startPc);
        emitU32(endPc);
        emitU32(loopValueName);
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
    uint32_t breakValue(std::string_view label) {
        uint32_t pc = emitOp(OpCode::BreakValue);
        emitU32(addString(std::string(label)));
        return pc;
    }
    uint32_t continueValue(std::string_view label) {
        uint32_t pc = emitOp(OpCode::ContinueValue);
        emitU32(addString(std::string(label)));
        return pc;
    }

    uint32_t pushScope() { return emitOp(OpCode::PushScope); }
    // 把当前作用域标注为严格模式（严格 eval 的 eval 词法作用域）。
    uint32_t setScopeStrict() { return emitOp(OpCode::SetScopeStrict); }
    uint32_t pushVarScope() { return emitOp(OpCode::PushVarScope); }
    uint32_t popScope() { return emitOp(OpCode::PopScope); }
    uint32_t pushWithScope() { return emitOp(OpCode::PushWithScope); }

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
    uint32_t setParamNames(const std::vector<std::string>& names) {
        uint32_t pc = emitOp(OpCode::SetParamNames);
        emitU32(static_cast<uint32_t>(names.size()));
        for (const std::string& name : names) emitU32(addString(name));
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
    uint32_t setFuncNameBinding(std::string_view name) {
        uint32_t pc = emitOp(OpCode::SetFuncNameBinding);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t setFuncNameIfAbsent(std::string_view name) {
        uint32_t pc = emitOp(OpCode::SetFuncNameIfAbsent);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t bodyStart() { return emitOp(OpCode::BodyStart); }
    uint32_t toPropertyKey() { return emitOp(OpCode::ToPropertyKey); }
    uint32_t toNumeric() { return emitOp(OpCode::ToNumeric); }
    uint32_t throwTypeError(std::string_view message) {
        uint32_t pc = emitOp(OpCode::ThrowTypeError);
        emitU32(addString(std::string(message)));
        return pc;
    }
    uint32_t throwReferenceError(std::string_view message) {
        uint32_t pc = emitOp(OpCode::ThrowReferenceError);
        emitU32(addString(std::string(message)));
        return pc;
    }
    uint32_t setFuncNameFromKey(std::string_view prefix = {}) {
        uint32_t pc = emitOp(OpCode::SetFuncNameFromKey);
        emitU32(addString(std::string(prefix)));
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
    // 直接 eval 调用点：栈上 [callee, args...]；callee 不是内建 eval 时
    // 按普通调用处理。
    uint32_t directEval(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::DirectEval);
        emitU32(argc);
        return pc;
    }
    // 尾位置的直接 eval 调用点：callee 不是内建 eval 时按尾调用处理。
    uint32_t directEvalTail(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::DirectEvalTail);
        emitU32(argc);
        return pc;
    }
    // 带展开的直接 eval 调用点：栈上 [callee, argsArray]。
    uint32_t directEvalSpread() {
        return emitOp(OpCode::DirectEvalSpread);
    }
    uint32_t callMethod(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::CallMethod);
        emitU32(argc);
        return pc;
    }
    uint32_t callSpread() { return emitOp(OpCode::CallSpread); }
    uint32_t callMethodSpread() { return emitOp(OpCode::CallMethodSpread); }
    uint32_t tailCall(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::TailCall);
        emitU32(argc);
        return pc;
    }
    uint32_t tailCallMethod(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::TailCallMethod);
        emitU32(argc);
        return pc;
    }
    uint32_t construct(uint32_t argc) {
        uint32_t pc = emitOp(OpCode::New);
        emitU32(argc);
        return pc;
    }
    uint32_t constructSpread() { return emitOp(OpCode::NewSpread); }
    uint32_t loadNewTarget() { return emitOp(OpCode::LoadNewTarget); }
    uint32_t importMeta() { return emitOp(OpCode::ImportMeta); }
    uint32_t dynamicImport() { return emitOp(OpCode::DynamicImport); }

    uint32_t yieldValue() { return emitOp(OpCode::Yield); }
    uint32_t yieldDelegate() { return emitOp(OpCode::YieldDelegate); }
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
    uint32_t markCatchScope() { return emitOp(OpCode::MarkCatchScope); }
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
    uint32_t setSuperPropKeep() { return emitOp(OpCode::SetSuperPropKeep); }
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
    // —— class 私有元素 ——
    uint32_t definePrivateField(std::string_view name, uint32_t flags = 0) {
        uint32_t pc = emitOp(OpCode::DefinePrivateField);
        emitU32(addString(std::string(name)));
        emitU32(flags);
        return pc;
    }
    uint32_t definePrivateMethod(std::string_view name, uint32_t flags) {
        uint32_t pc = emitOp(OpCode::DefinePrivateMethod);
        emitU32(addString(std::string(name)));
        emitU32(flags);
        return pc;
    }
    uint32_t getPrivateField(std::string_view name) {
        uint32_t pc = emitOp(OpCode::GetPrivateField);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t getPrivateMethod(std::string_view name) {
        uint32_t pc = emitOp(OpCode::GetPrivateMethod);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t getPrivateGetter(std::string_view name) {
        uint32_t pc = emitOp(OpCode::GetPrivateGetter);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t setPrivateField(std::string_view name) {
        uint32_t pc = emitOp(OpCode::SetPrivateField);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t setPrivateSetter(std::string_view name) {
        uint32_t pc = emitOp(OpCode::SetPrivateSetter);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t setPrivateFieldKeep(std::string_view name) {
        uint32_t pc = emitOp(OpCode::SetPrivateFieldKeep);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t setPrivateSetterKeep(std::string_view name) {
        uint32_t pc = emitOp(OpCode::SetPrivateSetterKeep);
        emitU32(addString(std::string(name)));
        return pc;
    }
    uint32_t setOwnerClass() { return emitOp(OpCode::SetOwnerClass); }
    uint32_t privateIn(std::string_view name) {
        uint32_t pc = emitOp(OpCode::PrivateIn);
        emitU32(addString(std::string(name)));
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
