// types/IntlListFormat.h - Intl.ListFormat 实例（ECMA-402 §13）
//
// 普通对象布局 + 宿主后端列表格式化器句柄。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_LIST_FORMAT_H
#define NEO_CORE_PRIVATE_TYPES_INTL_LIST_FORMAT_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "neojs.h"

#include <string>

namespace neo::core {

class Value;

class IntlListFormatType : public ObjectType {
public:
    IntlListFormatType() : ObjectType(TypeKind::IntlListFormat) {}
    const char* name() const override { return "Intl.ListFormat"; }
};

class IntlListFormatData : public ObjectData {
public:
    const NJS_IntlBackend* backend = nullptr;
    NJS_IntlListFormat* formatter = nullptr;

    std::string locale;
    std::string type = "conjunction";
    std::string style = "long";

    ~IntlListFormatData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlListFormatData* asIntlListFormat(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_LIST_FORMAT_H
