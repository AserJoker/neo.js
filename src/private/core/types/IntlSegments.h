// types/IntlSegments.h - Intl.Segmenter.prototype.segment 返回的 Segments 对象
// （ECMA-402 §18.4）
//
// 普通对象布局 + 预计算的分段结果。records 为 {segment,index,input,isWordLike}
// 记录数组，供 @@iterator 复用。

#ifndef NEO_CORE_PRIVATE_TYPES_INTL_SEGMENTS_H
#define NEO_CORE_PRIVATE_TYPES_INTL_SEGMENTS_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

#include <cstddef>
#include <string>
#include <vector>

namespace neo::core {

class Value;

class IntlSegmentsType : public ObjectType {
public:
    IntlSegmentsType() : ObjectType(TypeKind::IntlSegments) {}
    const char* name() const override { return "Intl.Segments"; }
};

class IntlSegmentsData : public ObjectData {
public:
    std::string input;
    std::vector<size_t> starts;
    std::vector<size_t> lengths;
    Value* records = nullptr;  // 记录数组（GC 托管）

    ~IntlSegmentsData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

IntlSegmentsData* asIntlSegments(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTL_SEGMENTS_H
