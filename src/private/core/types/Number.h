// types/number.h - NumberType + NumberData
//
// number 类型：持 double 负载。行为方法（add/sub/mul/div/to_string/...）待 Context
// 引入后补充——预计本文件会随 NumberType 行为实现大量膨胀。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_NUMBER_H
#define NEO_CORE_PRIVATE_TYPES_NUMBER_H

#include "core/IData.h"
#include "core/IType.h"

namespace neo::core {

class NumberType : public IType {
public:
    NumberType() : IType(TypeKind::Number) {}
    const char* name() const override { return "number"; }
};

// 持 double 的扁平 data
class NumberData : public IData {
public:
    double value;
    explicit NumberData(double v) : value(v) {}
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_NUMBER_H
