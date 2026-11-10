// ModuleNamespace.cpp - 模块命名空间 exotic 对象实现
//
// 关联 ADR: 018（对象模型）、044（可达性标记）

#include "core/types/ModuleNamespace.h"

#include "core/IGCEngine.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

void ModuleNamespaceData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    // 间接导出持有源命名空间与导出键，需保活。
    for (auto& [name, exp] : indirectExports) {
        (void)name;
        if (exp.namespaceObj != nullptr) visitor->visit(exp.namespaceObj);
        if (exp.exportKey != nullptr) visitor->visit(exp.exportKey);
    }
}

Value* ModuleNamespaceType::getProperty(IContext* ctx, Value* self, Value* key) {
    // 间接导出：读取时解析到源命名空间属性（支持 re-export 晚到导出）。
    if (key->type()->kind() != TypeKind::Symbol) {
        auto* d = static_cast<ModuleNamespaceData*>(self->data());
        std::string k;
        if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;
        auto it = d->indirectExports.find(k);
        if (it != d->indirectExports.end()) {
            return ctx->getProperty(it->second.namespaceObj, it->second.exportKey);
        }
    }
    return ObjectType::getProperty(ctx, self, key);
}

bool ModuleNamespaceType::hasProperty(IContext* ctx, Value* self, Value* key) {
    // 间接导出的名字已声明，即使源导出当前不存在也视为存在（[[Exports]] 语义）。
    if (key->type()->kind() != TypeKind::Symbol) {
        auto* d = static_cast<ModuleNamespaceData*>(self->data());
        std::string k;
        if (detail::propertyKeyString(ctx, key, k) != nullptr) return false;
        if (d->indirectExports.find(k) != d->indirectExports.end()) return true;
    }
    return ObjectType::hasProperty(ctx, self, key);
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
    auto* d = static_cast<ModuleNamespaceData*>(self->data());

    // symbol 键（如 @@toStringTag）：复用对象存储，直接存 Value*。
    if (key->type()->kind() == TypeKind::Symbol) {
        return ObjectType::defineProperty(ctx, self, key, value, false, false, true);
    }

    std::string k;
    if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;

    // 同名直接导出覆盖间接导出（内部 API 语义：后定义者生效）。
    d->indirectExports.erase(k);

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

Value* defineIndirectExport(IContext* ctx, Value* exports, Value* key,
                            Value* sourceNamespace, Value* sourceExportKey) {
    auto* d = static_cast<ModuleNamespaceData*>(exports->data());

    std::string k;
    if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;

    auto it = d->indirectExports.find(k);
    if (it != d->indirectExports.end()) {
        it->second = IndirectExport{sourceNamespace, sourceExportKey};
    } else {
        d->indirectExports.emplace(k, IndirectExport{sourceNamespace, sourceExportKey});
        // 占位 Property 让 ownPropertyKeys 能枚举到该导出名；读取由
        // ModuleNamespaceType::getProperty 拦截，不会用到占位 value。
        if (d->properties.find(k) == d->properties.end()) {
            d->properties.emplace(k, Property{nullptr, false, true, false, nullptr, nullptr});
            d->order.push_back(k);
        }
    }
    return ctx->getProperty(sourceNamespace, sourceExportKey);
}

}  // namespace neo::core
