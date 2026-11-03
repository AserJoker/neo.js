// types/null.h - NullType + NullData
//
// null 类型：空负载（仅占类型标识）。行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_NULL_H
#define NEO_CORE_PRIVATE_TYPES_NULL_H

#include "core/data.h"
#include "core/type.h"

namespace neo::core {

class NullType : public Type {
public:
    NullType() : Type(TypeKind::Null) {}
    const char* name() const override { return "null"; }
};

// 空负载 data
class NullData : public Data {};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_NULL_H
