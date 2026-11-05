// types/undefined.h - UndefinedType + UndefinedData
//
// undefined 类型：空负载（仅占类型标识）。行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_UNDEFINED_H
#define NEO_CORE_PRIVATE_TYPES_UNDEFINED_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <string_view>

namespace neo::core {

class UndefinedType : public IType {
public:
    UndefinedType() : IType(TypeKind::Undefined) {}
    const char* name() const override { return "undefined"; }

    // 行为方法（ADR-016 §3，实现见 Undefined.cpp）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* getProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* setProperty(IContext* ctx, Value* self, std::string_view key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* deleteProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* defineProperty(IContext* ctx, Value* self, std::string_view key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 空负载 data
class UndefinedData : public IData {
public:
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(UndefinedData));
        return new (p) UndefinedData();
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_UNDEFINED_H
