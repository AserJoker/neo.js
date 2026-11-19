// types/IntlPluralRules.cpp - Intl.PluralRules 实例生命周期

#include "core/types/IntlPluralRules.h"

#include "core/Value.h"

namespace neo::core {

IntlPluralRulesData::~IntlPluralRulesData() {
    if (rules != nullptr && backend != nullptr &&
        backend->plural_rules_destroy != nullptr) {
        backend->plural_rules_destroy(rules);
        rules = nullptr;
    }
}

void IntlPluralRulesData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlPluralRulesData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlPluralRulesData*>(this);
    self->refInc();
    return self;
}

IntlPluralRulesData* asIntlPluralRules(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlPluralRules) {
        return nullptr;
    }
    return static_cast<IntlPluralRulesData*>(v->data());
}

}  // namespace neo::core
