// types/IntlDateTimeFormat.h - Intl.DateTimeFormat 实例（ECMA-402 §15）
//
// 普通对象布局 + 宿主后端格式化器句柄。格式化器由 Data 析构时归还后端。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_DATE_TIME_FORMAT_H
#define NEO_CORE_PRIVATE_TYPES_INTL_DATE_TIME_FORMAT_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "neojs.h"

#include <string>

namespace neo::core {

class Value;

class IntlDateTimeFormatType : public ObjectType {
public:
    IntlDateTimeFormatType() : ObjectType(TypeKind::IntlDateTimeFormat) {}
    const char* name() const override { return "Intl.DateTimeFormat"; }
};

class IntlDateTimeFormatData : public ObjectData {
public:
    const NJS_IntlBackend* backend = nullptr;
    NJS_IntlFormatter* formatter = nullptr;

    std::string locale;
    std::string calendar = "gregory";
    std::string numberingSystem = "latn";
    std::string timeZone = "UTC";
    std::string dateStyle;
    std::string timeStyle;
    std::string hourCycle;
    std::string timeZoneName;
    bool hour12 = false;
    bool hasHour12 = false;
    bool hasYear = false;
    bool hasMonth = false;
    bool hasDay = false;
    bool hasHour = false;
    bool hasMinute = false;
    bool hasSecond = false;
    bool year2Digit = false;
    bool month2Digit = false;
    bool day2Digit = false;
    bool hour2Digit = false;
    bool minute2Digit = false;
    bool second2Digit = false;
    int fractionalSecondDigits = -1;

    ~IntlDateTimeFormatData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlDateTimeFormatData* asIntlDateTimeFormat(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_DATE_TIME_FORMAT_H
