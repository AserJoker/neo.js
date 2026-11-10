// ModuleNamespace.cpp - 模块命名空间 exotic 对象实现
//
// 关联 ADR: 018（对象模型）、044（可达性标记）

#include "core/types/ModuleNamespace.h"

#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

Value* ModuleNamespaceType::setProperty(IContext* ctx, Value* /*self*/, Value* key,
                                        Value* /*value*/) {
    return detail::typeError(
        ctx, "Cannot assign to read only property '" + detail::valueToString(key) +
                 "' of module namespace");
}

Value* ModuleNamespaceType::deleteProperty(IContext* ctx, Value* /*self*/, Value* key) {
    return detail::typeError(
        ctx, "Cannot delete property '" + detail::valueToString(key) +
                 "' of module namespace");
}

Value* ModuleNamespaceType::defineAccessor(IContext* ctx, Value* /*self*/, Value* key,
                                           Value* /*getter*/, Value* /*setter*/,
                                           bool /*enumerable*/, bool /*configurable*/) {
    return detail::typeError(
        ctx, "Cannot define accessor property '" + detail::valueToString(key) +
                 "' on module namespace");
}

Value* ModuleNamespaceType::setPrototype(IContext* ctx, Value* /*self*/, Value* /*proto*/) {
    return detail::typeError(ctx, "Cannot set prototype of module namespace");
}

Value* ModuleNamespaceType::defineProperty(IContext* ctx, Value* self, Value* key,
                                           Value* value, bool /*writable*/,
                                           bool /*enumerable*/, bool /*configurable*/) {
    auto* d = static_cast<ObjectData*>(self->data());

    // symbol 键（如 @@toStringTag）：复用对象存储，直接存 Value*。
    if (key->type()->kind() == TypeKind::Symbol) {
        return ObjectType::defineProperty(ctx, self, key, value, false, false, true);
    }

    std::string k;
    if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;

    // 活绑定：Property::value 直接指向模块作用域中的 Value*（不 clone）。
    // 强制导出属性不可写/不可配置。
    auto it = d->properties.find(k);
    if (it != d->properties.end()) {
        it->second.value = value;
        it->second.writable = false;
        it->second.enumerable = true;
        it->second.configurable = false;
        it->second.getter = nullptr;
        it->second.setter = nullptr;
        return value;
    }
    d->properties.emplace(
        k, Property{value, false, true, false, nullptr, nullptr});
    d->order.push_back(k);
    return value;
}

}  // namespace neo::core
