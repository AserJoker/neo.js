// types/symbol.h - SymbolType + SymbolData
//
// symbol 是原始类型（ES2024 §6.1.5）：唯一、不可变，用作属性键或元编程标识。
//   - description：人类可读描述（Symbol("x") 的 "x"，可空）
//
// 唯一性由 data 身份决定：每个 symbol 独占一个 SymbolData，且不可变类型按
// ADR-019 §4 共享 data（clone → ref++ 返回 this），故「data 指针相同」等价于
// 「同一个 symbol」，无须额外的 id 字段。
//
// 本阶段只实现数据类型本身：创建、相等性、toString、运算/调用/属性操作的分派。
// well-known symbols（Symbol.iterator 等）与 Symbol.for/keyFor 注册表、
// 以及属性键从 string 改为 Value* 留待后续阶段。
//
// 关联 ADR: 016（Type 体系）、017（类型清单）、018（对象模型 symbols 容器）、
//          019（赋值语义 §4 不可变类型共享 data）

#ifndef NEO_CORE_PRIVATE_TYPES_SYMBOL_H
#define NEO_CORE_PRIVATE_TYPES_SYMBOL_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <functional>
#include <new>
#include <string>
#include <string_view>

namespace neo::core {

class SymbolType : public IType {
public:
    SymbolType() : IType(TypeKind::Symbol) {}
    const char* name() const override { return "symbol"; }

    // 运算：symbol 不能隐式转换为原始值（ES2024 §7.1.1），全部返回 Exception。
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;

    // toString：返回 "Symbol(description)"（等价于 sym.toString()）。
    // 注意：字符串拼接 "x" + sym 仍抛错（由各类型的 add 拦截），不走此路径。
    Value* toString(IContext* ctx, Value* self) override;

    // 调用/构造：symbol 不是函数也不是构造函数。
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;

    // 属性/原型操作：原始类型无自有属性、无原型。
    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    Value* hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 符号负载：仅描述。不可变（构造后不再修改）。
// 唯一性 = data 身份（每个 symbol 独占一个 data，共享时 ref++ 复用同一 data）。
class SymbolData : public IData {
public:
    std::string description;  // 可空（Symbol() 的描述为空）

    explicit SymbolData(std::string_view desc) : description(desc) {}

    // 相等性：data 身份相同才是同一个 symbol（描述不参与判定）
    bool equals(const SymbolData* other) const { return this == other; }
    size_t hash() const { return std::hash<const SymbolData*>{}(this); }

    // clone：不可变类型共享 data（ADR-019 §4）——ref++ 后返回 this。
    IData* clone(neo::infra::IAllocator* /*alloc*/) const override {
        auto* self = const_cast<SymbolData*>(this);
        self->refInc();
        return self;
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_SYMBOL_H
