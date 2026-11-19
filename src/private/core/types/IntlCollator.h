// types/IntlCollator.h - Intl.Collator 实例（ECMA-402 §15）
//
// 普通对象布局 + 宿主后端排序器句柄。排序器由 Data 析构时归还后端。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_COLLATOR_H
#define NEO_CORE_PRIVATE_TYPES_INTL_COLLATOR_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "neojs.h"

#include <string>

namespace neo::core {

class Value;

class IntlCollatorType : public ObjectType {
public:
    IntlCollatorType() : ObjectType(TypeKind::IntlCollator) {}
    const char* name() const override { return "Intl.Collator"; }
};

class IntlCollatorData : public ObjectData {
public:
    const NJS_IntlBackend* backend = nullptr;
    NJS_IntlCollator* collator = nullptr;

    std::string locale;
    std::string usage = "sort";
    std::string sensitivity = "variant";
    std::string caseFirst = "false";
    std::string collation = "default";
    bool ignorePunctuation = false;
    bool numeric = false;

    ~IntlCollatorData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlCollatorData* asIntlCollator(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_COLLATOR_H
