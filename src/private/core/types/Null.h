// types/null.h - NullType + NullData
//
// null 类型：空负载（仅占类型标识）。行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_NULL_H
#define NEO_CORE_PRIVATE_TYPES_NULL_H

#include "core/IData.h"
#include "core/IType.h"

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
};

// 空负载 data
class NullData : public IData {};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_NULL_H
