// types/IntlRelativeTimeFormat.h - Intl.RelativeTimeFormat 实例（ECMA-402 §16）
//
// 普通对象布局 + 宿主后端相对时间格式化器句柄。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_RELATIVE_TIME_FORMAT_H
#define NEO_CORE_PRIVATE_TYPES_INTL_RELATIVE_TIME_FORMAT_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "neojs.h"

#include <string>

namespace neo::core {

class Value;

class IntlRelativeTimeFormatType : public ObjectType {
public:
    IntlRelativeTimeFormatType() : ObjectType(TypeKind::IntlRelativeTimeFormat) {}
    const char* name() const override { return "Intl.RelativeTimeFormat"; }
};

class IntlRelativeTimeFormatData : public ObjectData {
public:
    const NJS_IntlBackend* backend = nullptr;
    NJS_IntlRelativeTimeFormat* formatter = nullptr;

    std::string locale;
    std::string style = "long";
    std::string numeric = "always";
    std::string numberingSystem = "latn";

    ~IntlRelativeTimeFormatData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlRelativeTimeFormatData* asIntlRelativeTimeFormat(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_RELATIVE_TIME_FORMAT_H
