// types/IntlSegmenter.h - Intl.Segmenter 实例（ECMA-402 §18）
//
// 普通对象布局 + 宿主后端分段器句柄。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_SEGMENTER_H
#define NEO_CORE_PRIVATE_TYPES_INTL_SEGMENTER_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "neojs.h"

#include <string>

namespace neo::core {

class Value;

class IntlSegmenterType : public ObjectType {
public:
    IntlSegmenterType() : ObjectType(TypeKind::IntlSegmenter) {}
    const char* name() const override { return "Intl.Segmenter"; }
};

class IntlSegmenterData : public ObjectData {
public:
    const NJS_IntlBackend* backend = nullptr;
    NJS_IntlSegmenter* segmenter = nullptr;

    std::string locale;
    std::string granularity = "grapheme";

    ~IntlSegmenterData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlSegmenterData* asIntlSegmenter(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_SEGMENTER_H
