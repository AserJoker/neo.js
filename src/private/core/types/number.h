// types/number.h - NumberType + NumberData
//
// number 类型：持 double 负载。行为方法（add/sub/mul/div/to_string/...）待 Context
// 引入后补充——预计本文件会随 NumberType 行为实现大量膨胀。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_NUMBER_H
#define NEO_CORE_PRIVATE_TYPES_NUMBER_H

#include "core/data.h"
#include "core/type.h"

namespace neo::core {

class NumberType : public Type {
public:
    NumberType() : Type(TypeKind::Number) {}
    const char* name() const override { return "number"; }
};

// 持 double 的扁平 data
class NumberData : public Data {
public:
    double value;
    explicit NumberData(double v) : value(v) {}
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_NUMBER_H
