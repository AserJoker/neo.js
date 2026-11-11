// Proxy.cpp - Proxy exotic 对象实现（ES2024 §10.5 / §28.2）
//
// 所有内部方法先查 handler 上的同名 trap；trap 缺失（undefined/null）时
// 回退到 target 的普通对象算法。trap 抛出的异常经统一 Value* 通道透传。
//
// 关联 ADR: 018（对象模型）、048（Proxy 内部方法层）

#include "core/types/Proxy.h"

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace neo::core {

namespace {

ProxyData* asProxy(Value* v) {
    return static_cast<ProxyData*>(v->data());
}

// 查 handler[trapName]。present=false 表示 trap 缺失（回退默认行为）。
// 出错时返回 nullptr 并写入 err（已撤销 / handler 非对象 / trap 不可调用）。
Value* lookupTrap(IContext* ctx, ProxyData* d, const char* name, bool& present,
                  Value*& err) {
    present = false;
    err = nullptr;
    if (d->handler == nullptr) {
        err = detail::typeError(
            ctx, std::string("Cannot perform '") + name +
                     "' on a proxy that has been revoked");
        return nullptr;
    }
    Value* trap = ctx->getProperty(d->handler, ctx->createString(name));
    if (detail::isErrorValue(trap)) {
        err = trap;
        return nullptr;
    }
    TypeKind k = trap->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) return nullptr;
    if (!detail::isCallable(trap)) {
        err = detail::typeError(ctx, std::string("proxy trap '") + name +
                                         "' is not a function");
        return nullptr;
    }
    present = true;
    return trap;
}

// 把 getOwnPropertyDescriptor trap 返回的描述符对象转成 Property。
// 成功返回 nullptr；描述符非法返回 TypeError。
Value* descriptorToProperty(IContext* ctx, Value* descObj, bool& exists, Property& out) {
    exists = true;

    Value* enumerableV = ctx->getProperty(descObj, ctx->createString("enumerable"));
    if (detail::isErrorValue(enumerableV)) return enumerableV;
    Value* configurableV = ctx->getProperty(descObj, ctx->createString("configurable"));
    if (detail::isErrorValue(configurableV)) return configurableV;
    bool enumerable = detail::toBoolean(enumerableV);
    bool configurable = detail::toBoolean(configurableV);

    bool hasGet = ctx->hasProperty(descObj, ctx->createString("get"));
    bool hasSet = ctx->hasProperty(descObj, ctx->createString("set"));
    if (hasGet || hasSet) {
        Value* getter = ctx->getProperty(descObj, ctx->createString("get"));
        if (detail::isErrorValue(getter)) return getter;
        Value* setter = ctx->getProperty(descObj, ctx->createString("set"));
        if (detail::isErrorValue(setter)) return setter;
        auto normalize = [&](Value* v) -> Value* {
            if (v->type()->kind() == TypeKind::Undefined) return nullptr;
            if (!detail::isCallable(v)) {
                return detail::typeError(ctx, "Getter/setter must be a function");
            }
            return v;
        };
        Value* g = normalize(getter);
        if (detail::isErrorValue(g)) return g;
        Value* s = normalize(setter);
        if (detail::isErrorValue(s)) return s;
        out = Property{nullptr, false, enumerable, configurable, g, s};
        return nullptr;
    }

    Value* value = ctx->getProperty(descObj, ctx->createString("value"));
    if (detail::isErrorValue(value)) return value;
    Value* writableV = ctx->getProperty(descObj, ctx->createString("writable"));
    if (detail::isErrorValue(writableV)) return writableV;
    out = Property{value, detail::toBoolean(writableV), enumerable, configurable, nullptr,
                   nullptr};
    return nullptr;
}

// 把 Property 转成传给 defineProperty trap 的描述符对象。
Value* propertyToDescriptor(IContext* ctx, const Property& p) {
    Value* obj = ctx->createObject();
    if (obj == ctx->oomValue()) return obj;
    auto def = [&](const char* name, Value* v) -> Value* {
        Value* r = ctx->defineProperty(obj, ctx->createString(name), v, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (isAccessorDescriptor(p)) {
        Value* g = p.getter != nullptr ? p.getter : ctx->createUndefined();
        Value* s = p.setter != nullptr ? p.setter : ctx->createUndefined();
        if (Value* e = def("get", g)) return e;
        if (Value* e = def("set", s)) return e;
    } else {
        Value* v = p.value != nullptr ? p.value : ctx->createUndefined();
        if (Value* e = def("value", v)) return e;
        if (Value* e = def("writable", ctx->createBoolean(p.writable))) return e;
    }
    if (Value* e = def("enumerable", ctx->createBoolean(p.enumerable))) return e;
    if (Value* e = def("configurable", ctx->createBoolean(p.configurable))) return e;
    return obj;
}

// 把实参数组打包成 Array（apply/construct trap 的 argArray）。
Value* makeArgArray(IContext* ctx, size_t argc, Value* argv[]) {
    Value* arr = ctx->createArray();
    if (arr == ctx->oomValue()) return arr;
    for (size_t i = 0; i < argc; ++i) {
        Value* r = ctx->defineProperty(arr, ctx->createString(std::to_string(i)), argv[i],
                                       true, true, true);
        if (detail::isErrorValue(r)) return r;
    }
    return arr;
}

}  // namespace

void ProxyData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (target != nullptr) visitor->visit(target);
    if (handler != nullptr) visitor->visit(handler);
}

IData* ProxyData::clone(neo::infra::IAllocator* alloc) const {
    return ObjectData::clone(alloc);
}

Value* proxyTarget(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Proxy) return nullptr;
    return asProxy(v)->target;
}

Value* proxyHandler(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Proxy) return nullptr;
    return asProxy(v)->handler;
}

void revokeProxy(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Proxy) return;
    asProxy(v)->handler = nullptr;
}

// —— [[Call]] / [[Construct]] ——

Value* ProxyType::call(IContext* ctx, Value* self, Value* thisArg, size_t argc,
                       Value* argv[]) {
    auto* d = asProxy(self);
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "apply", present, err);
    if (err != nullptr) return err;
    if (!present) return ctx->call(d->target, thisArg, argc, argv);

    Value* args = makeArgArray(ctx, argc, argv);
    if (detail::isErrorValue(args)) return args;
    Value* callArgs[3] = {d->target, thisArg, args};
    return ctx->call(trap, d->handler, 3, callArgs);
}

Value* ProxyType::construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    auto* d = asProxy(self);
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "construct", present, err);
    if (err != nullptr) return err;
    Value* newTarget = ctx->newTarget();
    if (newTarget == nullptr) newTarget = self;
    if (!present) return ctx->construct(d->target, newTarget, argc, argv);

    Value* args = makeArgArray(ctx, argc, argv);
    if (detail::isErrorValue(args)) return args;
    Value* callArgs[3] = {d->target, args, newTarget};
    return ctx->call(trap, d->handler, 3, callArgs);
}

// —— 属性内部方法 ——

Value* ProxyType::getOwnProperty(IContext* ctx, Value* self, const PropertyKey& key,
                                 bool& exists, Property& out) const {
    auto* d = static_cast<ProxyData*>(self->data());
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "getOwnPropertyDescriptor", present, err);
    if (err != nullptr) return err;
    if (!present) {
        return d->target->type()->getOwnProperty(ctx, d->target, key, exists, out);
    }
    Value* argv[2] = {d->target, key.value};
    Value* r = ctx->call(trap, d->handler, 2, argv);
    if (detail::isErrorValue(r)) return r;
    if (r->type()->kind() == TypeKind::Undefined) {
        exists = false;
        return nullptr;
    }
    if (!detail::isObjectKind(r->type()->kind())) {
        return detail::typeError(ctx, "proxy getOwnPropertyDescriptor trap returned non-object");
    }
    return descriptorToProperty(ctx, r, exists, out);
}

Value* ProxyType::ownKeys(IContext* ctx, Value* self, std::vector<Value*>& out) const {
    auto* d = static_cast<ProxyData*>(self->data());
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "ownKeys", present, err);
    if (err != nullptr) return err;
    if (!present) return d->target->type()->ownKeys(ctx, d->target, out);

    Value* argv[1] = {d->target};
    Value* r = ctx->call(trap, d->handler, 1, argv);
    if (detail::isErrorValue(r)) return r;
    if (!detail::isObjectKind(r->type()->kind())) {
        return detail::typeError(ctx, "proxy ownKeys trap returned non-object");
    }
    uint32_t len = 0;
    if (Value* e = detail::lengthOf(ctx, r, len)) return e;
    std::unordered_set<std::string> stringKeys;
    std::unordered_set<const void*> symbolKeys;
    for (uint32_t i = 0; i < len; ++i) {
        Value* k = ctx->getProperty(r, ctx->createString(std::to_string(i)));
        if (detail::isErrorValue(k)) return k;
        TypeKind kk = k->type()->kind();
        if (kk == TypeKind::String) {
            auto* sd = static_cast<StringData*>(k->data());
            if (!stringKeys.insert(sd->value).second) {
                return detail::typeError(ctx, "proxy ownKeys trap returned duplicate keys");
            }
        } else if (kk == TypeKind::Symbol) {
            if (!symbolKeys.insert(k->data()).second) {
                return detail::typeError(ctx, "proxy ownKeys trap returned duplicate keys");
            }
        } else {
            return detail::typeError(ctx, "proxy ownKeys trap returned non-property-key");
        }
        out.push_back(k);
    }
    return nullptr;
}

Value* ProxyType::getWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                                  Value* receiver) {
    auto* d = asProxy(self);
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "get", present, err);
    if (err != nullptr) return err;
    if (!present) return ordinaryGet(ctx, d->target, key, receiver);
    Value* argv[3] = {d->target, key.value, receiver};
    return ctx->call(trap, d->handler, 3, argv);
}

Value* ProxyType::setWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                                  Value* value, Value* receiver, bool& ok) {
    auto* d = asProxy(self);
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "set", present, err);
    if (err != nullptr) return err;
    if (!present) return ordinarySet(ctx, d->target, key, value, receiver, ok);
    Value* argv[4] = {d->target, key.value, value, receiver};
    Value* r = ctx->call(trap, d->handler, 4, argv);
    if (detail::isErrorValue(r)) return r;
    ok = detail::toBoolean(r);
    return nullptr;
}

Value* ProxyType::getProperty(IContext* ctx, Value* self, Value* key) {
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    return getWithReceiver(ctx, self, pk, self);
}

Value* ProxyType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    bool ok = false;
    Value* err = setWithReceiver(ctx, self, pk, value, self, ok);
    if (err != nullptr) return err;
    if (!ok) {
        if (!ctx->hasFlag("strict")) return value;
        std::string name = pk.isSymbol ? detail::valueToString(key) : pk.name;
        return detail::typeError(ctx, "Cannot set property '" + name + "'");
    }
    return value;
}

bool ProxyType::hasProperty(IContext* ctx, Value* self, Value* key) {
    auto* d = asProxy(self);
    PropertyKey pk;
    if (toPropertyKey(ctx, key, pk) != nullptr) return false;
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "has", present, err);
    if (err != nullptr) return false;
    if (!present) return ordinaryHas(ctx, d->target, pk);
    Value* argv[2] = {d->target, pk.value};
    Value* r = ctx->call(trap, d->handler, 2, argv);
    if (detail::isErrorValue(r)) return false;
    return detail::toBoolean(r);
}

Value* ProxyType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    auto* d = asProxy(self);
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "deleteProperty", present, err);
    if (err != nullptr) return err;
    if (!present) {
        bool ok = false;
        Value* e = ordinaryDelete(ctx, d->target, pk, ok);
        if (e != nullptr) return e;
        return ctx->createBoolean(ok);
    }
    Value* argv[2] = {d->target, pk.value};
    Value* r = ctx->call(trap, d->handler, 2, argv);
    if (detail::isErrorValue(r)) return r;
    return ctx->createBoolean(detail::toBoolean(r));
}

Value* ProxyType::defineProperty(IContext* ctx, Value* self, Value* key, Value* value,
                                 bool writable, bool enumerable, bool configurable) {
    auto* d = asProxy(self);
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "defineProperty", present, err);
    if (err != nullptr) return err;
    if (!present) {
        return d->target->type()->defineProperty(ctx, d->target, pk.value, value, writable,
                                                 enumerable, configurable);
    }
    Property desc{value, writable, enumerable, configurable, nullptr, nullptr};
    Value* descObj = propertyToDescriptor(ctx, desc);
    if (detail::isErrorValue(descObj)) return descObj;
    Value* argv[3] = {d->target, pk.value, descObj};
    Value* r = ctx->call(trap, d->handler, 3, argv);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) {
        return detail::typeError(ctx, "proxy defineProperty trap returned false");
    }
    return value;
}

Value* ProxyType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                 Value* setter, bool enumerable, bool configurable) {
    auto* d = asProxy(self);
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "defineProperty", present, err);
    if (err != nullptr) return err;
    if (!present) {
        return d->target->type()->defineAccessor(ctx, d->target, pk.value, getter, setter,
                                                 enumerable, configurable);
    }
    Value* g = (getter != nullptr && getter->type()->kind() == TypeKind::Function)
                   ? getter
                   : nullptr;
    Value* s = (setter != nullptr && setter->type()->kind() == TypeKind::Function)
                   ? setter
                   : nullptr;
    Property desc{nullptr, false, enumerable, configurable, g, s};
    Value* descObj = propertyToDescriptor(ctx, desc);
    if (detail::isErrorValue(descObj)) return descObj;
    Value* argv[3] = {d->target, pk.value, descObj};
    Value* r = ctx->call(trap, d->handler, 3, argv);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) {
        return detail::typeError(ctx, "proxy defineProperty trap returned false");
    }
    return self;
}

Value* ProxyType::getPrototype(IContext* ctx, Value* self) {
    auto* d = asProxy(self);
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "getPrototypeOf", present, err);
    if (err != nullptr) return err;
    if (!present) return d->target->type()->getPrototype(ctx, d->target);
    Value* argv[1] = {d->target};
    Value* r = ctx->call(trap, d->handler, 1, argv);
    if (detail::isErrorValue(r)) return r;
    TypeKind k = r->type()->kind();
    if (k != TypeKind::Null && !detail::isObjectKind(k)) {
        return detail::typeError(ctx, "proxy getPrototypeOf trap returned non-object");
    }
    return r;
}

Value* ProxyType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    auto* d = asProxy(self);
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "setPrototypeOf", present, err);
    if (err != nullptr) return err;
    if (!present) return d->target->type()->setPrototype(ctx, d->target, proto);
    Value* argv[2] = {d->target, proto};
    Value* r = ctx->call(trap, d->handler, 2, argv);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) {
        return detail::typeError(ctx, "proxy setPrototypeOf trap returned false");
    }
    return self;
}

}  // namespace neo::core
