// types/IntlPluralRules.h - Intl.PluralRules 实例（ECMA-402 §15）
//
// 普通对象布局 + 宿主后端复数规则句柄。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_PLURAL_RULES_H
#define NEO_CORE_PRIVATE_TYPES_INTL_PLURAL_RULES_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "neojs.h"

#include <string>

namespace neo::core {

class Value;

class IntlPluralRulesType : public ObjectType {
public:
    IntlPluralRulesType() : ObjectType(TypeKind::IntlPluralRules) {}
    const char* name() const override { return "Intl.PluralRules"; }
};

class IntlPluralRulesData : public ObjectData {
public:
    const NJS_IntlBackend* backend = nullptr;
    NJS_IntlPluralRules* rules = nullptr;

    std::string locale;
    std::string type = "cardinal";
    std::string notation = "standard";
    int minimumIntegerDigits = 1;
    int minimumFractionDigits = 0;
    int maximumFractionDigits = 3;
    int minimumSignificantDigits = -1;
    int maximumSignificantDigits = -1;

    ~IntlPluralRulesData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlPluralRulesData* asIntlPluralRules(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_PLURAL_RULES_H
