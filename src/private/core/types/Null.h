// types/null.h - NullType + NullData
//
// null 类型：空负载（仅占类型标识）。行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_NULL_H
#define NEO_CORE_PRIVATE_TYPES_NULL_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <string_view>

namespace neo::core {

class NullType : public IType {
public:
    NullType() : IType(TypeKind::Null) {}
    const char* name() const override { return "null"; }

    // 行为方法（ADR-016 §3，实现见 Null.cpp）
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

// 空负载 data
class NullData : public IData {
public:
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(NullData));
        return new (p) NullData();
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_NULL_H
