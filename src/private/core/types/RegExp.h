// types/regexp.h - RegExpType + RegExpData（ES2024 §22.2）
//
// RegExp 是普通对象布局 + 不可变编译产物：
//   [[OriginalSource]] / [[OriginalFlags]] / shared_ptr<const regex::Program>
// lastIndex 按规范是普通自有数据属性，由 JS 层读写，不进入内部槽。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_CORE_PRIVATE_TYPES_REGEXP_H
#define NEO_CORE_PRIVATE_TYPES_REGEXP_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"
#include "regex/RegexProgram.h"

#include <memory>
#include <string>

namespace neo::core {

class Value;

class RegExpType : public ObjectType {
public:
    RegExpType() : ObjectType(TypeKind::RegExp) {}
    const char* name() const override { return "regexp"; }
};

class RegExpData : public ObjectData {
public:
    std::string source;
    std::string flags;
    std::shared_ptr<const neo::regex::Program> program;

    bool global = false;
    bool ignoreCase = false;
    bool multiline = false;
    bool dotAll = false;
    bool unicode = false;
    bool unicodeSets = false;
    bool sticky = false;
    bool hasIndices = false;

    // GC 遍历：只报告对象布局持有的子 value；Program 是纯 C++ 数据。
    void enumerateChildren(IGCVisitor* visitor) override {
        ObjectData::enumerateChildren(visitor);
    }

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this。
    IData* clone(neo::infra::IAllocator* /*alloc*/) const override {
        auto* self = const_cast<RegExpData*>(this);
        self->refInc();
        return self;
    }
};

// 取 RegExpData；非 RegExp 返回 nullptr。
inline RegExpData* regexpData(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::RegExp) return nullptr;
    return static_cast<RegExpData*>(v->data());
}

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_REGEXP_H
