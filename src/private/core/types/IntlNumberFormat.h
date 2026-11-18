// types/IntlNumberFormat.h - Intl.NumberFormat 实例（ECMA-402 §15）
//
// 普通对象布局 + 宿主后端格式化器句柄。格式化器由 Data 析构时归还后端；
// 其余属性/原型/GC 行为与普通对象一致。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、020（标准库组织）

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_NUMBER_FORMAT_H
#define NEO_CORE_PRIVATE_TYPES_INTL_NUMBER_FORMAT_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "neojs.h"

#include <string>

namespace neo::core {

class Value;

class IntlNumberFormatType : public ObjectType {
public:
    IntlNumberFormatType() : ObjectType(TypeKind::IntlNumberFormat) {}
    const char* name() const override { return "Intl.NumberFormat"; }
};

class IntlNumberFormatData : public ObjectData {
public:
    const NJS_IntlBackend* backend = nullptr;
    NJS_IntlFormatter* formatter = nullptr;

    // resolvedOptions 的引擎侧快照（后端归一化后的值）。
    std::string locale;
    std::string numberingSystem = "latn";
    std::string style = "decimal";
    std::string currency;
    std::string currencyDisplay = "symbol";
    std::string notation = "standard";
    std::string signDisplay = "auto";
    bool useGrouping = true;
    int minimumIntegerDigits = 1;
    int minimumFractionDigits = 0;
    int maximumFractionDigits = 3;

    ~IntlNumberFormatData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlNumberFormatData* asIntlNumberFormat(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_NUMBER_FORMAT_H
