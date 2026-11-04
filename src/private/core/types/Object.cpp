// Object.cpp - ObjectType 行为方法实现 + ObjectData GC 遍历
//
// 属性操作（ADR-018）：
//   getProperty    —— 自有属性查找（本阶段不含原型链，阶段 5）
//   setProperty    —— 存在且 writable 则改值；不存在则新建（writable/enumerable/configurable 全 true）
//   hasProperty    —— 自有属性存在性
//   deleteProperty —— 从 map 与 order 同步移除（O(n)），返回 true（成功）/ Exception（不可配置）
//   defineProperty —— 完整描述符定义
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Object.h"

#include <string>

namespace neo::core {

namespace {

// 非对象运算占位错误（对象参与运算需 ToPrimitive，后续阶段）
Value* notImplemented(IContext* ctx, const char* op) {
    std::string msg = std::string("Object.") + op + " is not implemented";
    return ctx->createException(ctx->createString(msg));
}

}  // namespace

// —— 运算：本阶段未实现 ——

Value* ObjectType::add(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "add");
}
Value* ObjectType::sub(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "sub");
}
Value* ObjectType::mul(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "mul");
}
Value* ObjectType::div(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "div");
}
Value* ObjectType::toString(IContext* ctx, Value* /*self*/) {
    return notImplemented(ctx, "toString");
}

Value* ObjectType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                        size_t /*argc*/, Value* /*argv*/[]) {
    std::string msg = std::string(self->type()->name()) + " is not a function";
    return ctx->createException(ctx->createString(msg));
}

// —— 属性操作 ——

Value* ObjectType::getProperty(IContext* ctx, Value* self, std::string_view key) {
    auto* d = static_cast<ObjectData*>(self->data());
    auto it = d->properties.find(std::string(key));
    if (it == d->properties.end()) {
        // 本阶段不含原型链查找，未找到返回 undefined
        return ctx->createUndefined();
    }
    // getter/setter 阶段 5 启用；本阶段只返回数据属性 value
    Value* v = it->second.value;
    return v != nullptr ? v : ctx->createUndefined();
}

Value* ObjectType::setProperty(IContext* ctx, Value* self, std::string_view key, Value* value) {
    auto* d = static_cast<ObjectData*>(self->data());
    std::string k(key);
    auto it = d->properties.find(k);
    if (it != d->properties.end()) {
        // 已存在：writable 才可改
        if (!it->second.writable) {
            std::string msg = "Cannot assign to read only property '" + k + "'";
            return ctx->createException(ctx->createString(msg));
        }
        it->second.value = value;
        return value;
    }
    // 不存在：新建（默认全 true），追加 order
    d->properties.emplace(k, Property{value, true, true, true, nullptr, nullptr});
    d->order.push_back(k);
    return value;
}

bool ObjectType::hasProperty(IContext* /*ctx*/, Value* self, std::string_view key) {
    // 自有属性存在性（不含原型链，阶段 5）
    auto* d = static_cast<ObjectData*>(self->data());
    return d->properties.find(std::string(key)) != d->properties.end();
}

Value* ObjectType::deleteProperty(IContext* ctx, Value* self, std::string_view key) {
    auto* d = static_cast<ObjectData*>(self->data());
    std::string k(key);
    auto it = d->properties.find(k);
    if (it == d->properties.end()) {
        // 不存在：delete 视为成功（JS 语义）
        return ctx->createBoolean(true);
    }
    if (!it->second.configurable) {
        std::string msg = "Cannot delete property '" + k + "'";
        return ctx->createException(ctx->createString(msg));
    }
    // 从 map 与 order 同步移除（O(n)，ADR-018 §4）
    d->properties.erase(it);
    for (auto oit = d->order.begin(); oit != d->order.end(); ++oit) {
        if (*oit == k) {
            d->order.erase(oit);
            break;
        }
    }
    return ctx->createBoolean(true);
}

Value* ObjectType::defineProperty(IContext* /*ctx*/, Value* self, std::string_view key,
                                  Value* value, bool writable, bool enumerable,
                                  bool configurable) {
    auto* d = static_cast<ObjectData*>(self->data());
    std::string k(key);
    auto it = d->properties.find(k);
    if (it != d->properties.end()) {
        // 已存在：更新描述符
        it->second.value = value;
        it->second.writable = writable;
        it->second.enumerable = enumerable;
        it->second.configurable = configurable;
        return value;
    }
    d->properties.emplace(k, Property{value, writable, enumerable, configurable, nullptr, nullptr});
    d->order.push_back(k);
    return value;
}

// —— ObjectData ——

void ObjectData::enumerateChildren(IGCVisitor* visitor) {
    // 原型链
    if (prototype != nullptr) visitor->visit(prototype);
    // 所有 property 的 value/getter/setter
    for (auto& [key, prop] : properties) {
        (void)key;
        if (prop.value != nullptr) visitor->visit(prop.value);
        if (prop.getter != nullptr) visitor->visit(prop.getter);
        if (prop.setter != nullptr) visitor->visit(prop.setter);
    }
    // symbol 属性（本阶段容器为空，保留遍历逻辑）
    for (auto& [sym, prop] : symbols) {
        (void)sym;
        if (prop.value != nullptr) visitor->visit(prop.value);
        if (prop.getter != nullptr) visitor->visit(prop.getter);
        if (prop.setter != nullptr) visitor->visit(prop.setter);
    }
}

IData* ObjectData::clone(neo::infra::IAllocator* alloc) const {
    void* p = alloc->alloc(sizeof(ObjectData));
    auto* copy = new (p) ObjectData();
    // 浅拷贝容器（属性 value 指针共享，GC 管理生命周期）
    copy->properties = properties;
    copy->order = order;
    copy->symbols = symbols;
    copy->prototype = prototype;
    return copy;
}

}  // namespace neo::core
