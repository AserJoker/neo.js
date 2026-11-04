// types/boolean.h - BooleanType + BooleanData
//
// boolean 类型：持 bool 负载。行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_BOOLEAN_H
#define NEO_CORE_PRIVATE_TYPES_BOOLEAN_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>

namespace neo::core {

class BooleanType : public IType {
public:
    BooleanType() : IType(TypeKind::Boolean) {}
    const char* name() const override { return "boolean"; }

    // 行为方法（ADR-016 §3，实现见 Boolean.cpp）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;
};

// 持 bool 的扁平 data
class BooleanData : public IData {
public:
    bool value;
    explicit BooleanData(bool v) : value(v) {}
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(BooleanData));
        return new (p) BooleanData(value);
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_BOOLEAN_H
