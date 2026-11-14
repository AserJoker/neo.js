// ModuleNamespace.cpp - 模块命名空间 exotic 对象实现
//
// 关联 ADR: 018（对象模型）、044（可达性标记）

#include "core/types/ModuleNamespace.h"

#include "core/IGCEngine.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

Value* ModuleNamespaceType::getProperty(IContext* ctx, Value* self, Value* key) {
    Value* value = ObjectType::getProperty(ctx, self, key);
    if (detail::isErrorValue(value)) return value;
    // 导出槽位尚未初始化（TDZ）：namespace 读取同样抛 ReferenceError。
    if (value != nullptr && value->type()->kind() == TypeKind::Uninitialized) {
        return detail::referenceError(
            ctx, "Cannot access '" + detail::valueToString(key) +
                     "' before initialization");
    }
    return value;
}

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

Value* ModuleNamespaceType::defineAccessor(IContext* ctx, Value* /*self*/, Value* /*key*/,
                                           Value* /*getter*/, Value* /*setter*/,
                                           bool /*enumerable*/, bool /*configurable*/) {
    return ctx->createBoolean(false);
}

Value* ModuleNamespaceType::setPrototype(IContext* ctx, Value* /*self*/, Value* /*proto*/) {
    return ctx->createBoolean(false);
}

Value* ModuleNamespaceType::defineProperty(IContext* ctx, Value* self, Value* key,
                                           Value* value, bool /*writable*/,
                                           bool /*enumerable*/, bool /*configurable*/) {
    auto* d = static_cast<ModuleNamespaceData*>(self->data());

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
        return ctx->createBoolean(true);
    }
    d->properties.emplace(
        k, Property{value, false, true, false, nullptr, nullptr});
    d->order.push_back(k);
    return ctx->createBoolean(true);
}

Value* findExportSlot(Value* exports, std::string_view name) {
    if (exports == nullptr ||
        exports->type()->kind() != TypeKind::ModuleNamespace) {
        return nullptr;
    }
    auto* d = static_cast<ModuleNamespaceData*>(exports->data());
    auto it = d->properties.find(std::string(name));
    if (it == d->properties.end()) return nullptr;
    return it->second.value;
}

}  // namespace neo::core
