// types/BigInt.h - BigIntType + BigIntData
//
// bigint 原始值（ES2024 §6.1.4）：持 neo::bigint::BigInt 负载，不可变。
// 无对象布局：属性访问走 ToObject（包装对象在 BigIntBuiltins 中提供）。
//
// 关联 ADR: 016（Type 体系）、050（BigInt 基础设施）、051（引擎接入）

#ifndef NEO_CORE_PRIVATE_TYPES_BIGINT_H
#define NEO_CORE_PRIVATE_TYPES_BIGINT_H

#include "bigint/BigInt.h"
#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <utility>

namespace neo::core {

class BigIntType : public IType {
public:
    BigIntType() : IType(TypeKind::BigInt) {}
    const char* name() const override { return "bigint"; }

    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;
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

// 不可变负载：clone 共享 data（ref++ 后返回 this），与 StringData 同策略。
class BigIntData : public IData {
public:
    neo::bigint::BigInt value;
    explicit BigIntData(neo::bigint::BigInt v) : value(std::move(v)) {}
    IData* clone(neo::infra::IAllocator* /*alloc*/) const override {
        auto* self = const_cast<BigIntData*>(this);
        self->refInc();
        return self;
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_BIGINT_H
