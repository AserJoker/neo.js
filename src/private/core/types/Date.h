// types/Date.h - Date 对象（ES2024 §21.4）
//
// Date 是普通对象 + [[DateValue]] 内部槽：毫秒时间值（自 1970-01-01T00:00:00Z），
// NaN 表示 Invalid Date。属性/原型语义复用 ObjectData。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#ifndef NEO_CORE_PRIVATE_TYPES_DATE_H
#define NEO_CORE_PRIVATE_TYPES_DATE_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

namespace neo::core {

class Value;

class DateType : public ObjectType {
public:
    DateType() : ObjectType(TypeKind::Date) {}
    const char* name() const override { return "date"; }
};

class DateData : public ObjectData {
public:
    double timeValue = 0.0;  // [[DateValue]]，NaN = Invalid Date

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

DateData* asDate(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_DATE_H
