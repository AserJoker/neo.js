// types/IntlDisplayNames.h - Intl.DisplayNames 实例（ECMA-402 §12）
//
// 普通对象布局 + 宿主后端显示名解析器句柄。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_DISPLAY_NAMES_H
#define NEO_CORE_PRIVATE_TYPES_INTL_DISPLAY_NAMES_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "neojs.h"

#include <string>

namespace neo::core {

class Value;

class IntlDisplayNamesType : public ObjectType {
public:
    IntlDisplayNamesType() : ObjectType(TypeKind::IntlDisplayNames) {}
    const char* name() const override { return "Intl.DisplayNames"; }
};

class IntlDisplayNamesData : public ObjectData {
public:
    const NJS_IntlBackend* backend = nullptr;
    NJS_IntlDisplayNames* names = nullptr;

    std::string locale;
    std::string type = "language";
    std::string style = "long";
    std::string fallback = "code";
    std::string languageDisplay = "dialect";

    ~IntlDisplayNamesData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlDisplayNamesData* asIntlDisplayNames(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_DISPLAY_NAMES_H
