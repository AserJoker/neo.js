// types/undefined.h - UndefinedType + UndefinedData
//
// undefined 类型：空负载（仅占类型标识）。行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_UNDEFINED_H
#define NEO_CORE_PRIVATE_TYPES_UNDEFINED_H

#include "core/data.h"
#include "core/type.h"

namespace neo::core {

class UndefinedType : public Type {
public:
    UndefinedType() : Type(TypeKind::Undefined) {}
    const char* name() const override { return "undefined"; }
};

// 空负载 data
class UndefinedData : public Data {};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_UNDEFINED_H
