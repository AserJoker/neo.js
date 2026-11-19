// types/IntlLocale.h - Intl.Locale 实例（ECMA-402 §14）
//
// 普通对象布局 + 规范内部槽的字符串化表示：
//   [[Locale]]     → localeName（含 unicode 扩展的完整规范标签）
//   [[TimeZone]]   → timeZone（空串表示 undefined）
// 其余内部槽（calendar/collation/hourCycle/numberingSystem/caseFirst/numeric）
// 由 unicode 扩展关键字承载，读取时从 localeName 解析。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_LOCALE_H
#define NEO_CORE_PRIVATE_TYPES_INTL_LOCALE_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

#include <string>

namespace neo::core {

class Value;

class IntlLocaleType : public ObjectType {
public:
    IntlLocaleType() : ObjectType(TypeKind::IntlLocale) {}
    const char* name() const override { return "Intl.Locale"; }
};

class IntlLocaleData : public ObjectData {
public:
    std::string localeName;
    std::string timeZone;  // 空串 = [[TimeZone]] 为 undefined

    ~IntlLocaleData() override = default;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlLocaleData* asIntlLocale(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_LOCALE_H
