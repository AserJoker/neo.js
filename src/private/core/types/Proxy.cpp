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

// 规范属性键相等（String 按内容，Symbol 按身份）。
bool keyEquals(Value* a, Value* b) {
    TypeKind ak = a->type()->kind();
    if (ak != b->type()->kind()) return false;
    if (ak == TypeKind::Symbol) return a->data() == b->data();
    if (ak == TypeKind::String) {
        return static_cast<StringData*>(a->data())->value ==
               static_cast<StringData*>(b->data())->value;
    }
    return false;
}

bool keyListContains(const std::vector<Value*>& keys, Value* key) {
    for (Value* k : keys) {
        if (keyEquals(k, key)) return true;
    }
    return false;
}

// 取 target 的自有描述符；成功返回 nullptr。
Value* targetOwnDescriptor(IContext* ctx, Value* target, const PropertyKey& key,
                           bool& exists, Property& out) {
    return target->type()->getOwnProperty(ctx, target, key, exists, out);
}

Value* targetExtensible(IContext* ctx, Value* target) {
    return target->type()->isExtensible(ctx, target);
}

// 数据属性值（nullptr 视为 undefined）。
Value* dataValueOrUndefined(IContext* ctx, const Property& p) {
    return p.value != nullptr ? p.value : ctx->createUndefined();
}

// defineProperty trap 成功后的不变量（ES2024 §10.5.6）。
Value* checkDefineInvariant(IContext* ctx, Value* target, const PropertyKey& key,
                            const Property& desc) {
    Property targetDesc;
    bool targetHas = false;
    if (Value* e = targetOwnDescriptor(ctx, target, key, targetHas, targetDesc)) return e;
    if (!targetHas) {
        Value* ext = targetExtensible(ctx, target);
        if (detail::isErrorValue(ext)) return ext;
        if (!detail::toBoolean(ext)) {
            return detail::typeError(
                ctx, "proxy defineProperty trap cannot add a property to a non-extensible target");
        }
        return nullptr;
    }
    if (!isCompatiblePropertyDescriptor(targetDesc, desc)) {
        return detail::typeError(
            ctx, "proxy defineProperty trap returned success for an incompatible descriptor");
    }
    return nullptr;
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
    exists = r->type()->kind() != TypeKind::Undefined;
    if (exists) {
        if (!detail::isObjectKind(r->type()->kind())) {
            return detail::typeError(
                ctx, "proxy getOwnPropertyDescriptor trap returned non-object");
        }
        if (Value* e = propertyDescriptorFromValue(ctx, r, out)) return e;
    }

    // —— 不变量（ES2024 §10.5.5）——
    Property targetDesc;
    bool targetHas = false;
    if (Value* e = targetOwnDescriptor(ctx, d->target, key, targetHas, targetDesc)) return e;
    if (!exists) {
        if (targetHas) {
            if (!targetDesc.configurable) {
                return detail::typeError(
                    ctx, "proxy getOwnPropertyDescriptor trap cannot hide a non-configurable property");
            }
            Value* ext = targetExtensible(ctx, d->target);
            if (detail::isErrorValue(ext)) return ext;
            if (!detail::toBoolean(ext)) {
                return detail::typeError(
                    ctx, "proxy getOwnPropertyDescriptor trap cannot hide a property of a non-extensible target");
            }
        }
        return nullptr;
    }
    if (!targetHas) {
        Value* ext = targetExtensible(ctx, d->target);
        if (detail::isErrorValue(ext)) return ext;
        if (!detail::toBoolean(ext)) {
            return detail::typeError(
                ctx, "proxy getOwnPropertyDescriptor trap cannot report a new property on a non-extensible target");
        }
        if (!out.configurable) {
            return detail::typeError(
                ctx, "proxy getOwnPropertyDescriptor trap cannot report a non-configurable new property");
        }
    } else {
        if (!out.configurable && targetDesc.configurable) {
            return detail::typeError(
                ctx, "proxy getOwnPropertyDescriptor trap cannot report a non-configurable property as configurable");
        }
        if (!isCompatiblePropertyDescriptor(targetDesc, out)) {
            return detail::typeError(
                ctx, "proxy getOwnPropertyDescriptor trap returned an incompatible descriptor");
        }
    }
    return nullptr;
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

    // —— 不变量（ES2024 §10.5.11）——
    std::vector<Value*> targetKeys;
    if (Value* e = d->target->type()->ownKeys(ctx, d->target, targetKeys)) return e;
    Value* ext = targetExtensible(ctx, d->target);
    if (detail::isErrorValue(ext)) return ext;
    bool targetExtensibleFlag = detail::toBoolean(ext);

    if (!targetExtensibleFlag) {
        // 不可扩展 target：trap 结果必须与 target 自有键完全一致。
        if (out.size() != targetKeys.size()) {
            return detail::typeError(
                ctx, "proxy ownKeys trap must return all keys of a non-extensible target");
        }
        for (Value* tk : targetKeys) {
            if (!keyListContains(out, tk)) {
                return detail::typeError(
                    ctx, "proxy ownKeys trap must return all keys of a non-extensible target");
            }
        }
    } else {
        // 可扩展 target：必须包含所有不可配置自有键。
        for (Value* tk : targetKeys) {
            PropertyKey pk;
            if (Value* e = toPropertyKey(ctx, tk, pk)) return e;
            Property desc;
            bool exists = false;
            if (Value* e = targetOwnDescriptor(ctx, d->target, pk, exists, desc)) return e;
            if (!exists || desc.configurable) continue;
            if (!keyListContains(out, tk)) {
                return detail::typeError(
                    ctx, "proxy ownKeys trap must include non-configurable keys of target");
            }
        }
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
    Value* r = ctx->call(trap, d->handler, 3, argv);
    if (detail::isErrorValue(r)) return r;

    // —— 不变量（ES2024 §10.5.8）——
    // target 有不可配置、不可写的数据属性时，trap 结果必须与 target 值相同。
    Property targetDesc;
    bool targetHas = false;
    if (Value* e = targetOwnDescriptor(ctx, d->target, key, targetHas, targetDesc)) return e;
    if (targetHas && !isAccessorDescriptor(targetDesc) && !targetDesc.configurable &&
        !targetDesc.writable) {
        Value* tv = dataValueOrUndefined(ctx, targetDesc);
        if (!detail::sameValue(r, tv)) {
            return detail::typeError(
                ctx, "proxy get trap returned a different value for a non-writable, non-configurable property");
        }
    }
    return r;
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

    // —— 不变量（ES2024 §10.5.9）——
    // target 有不可配置、不可写的数据属性时，不能报告成功写入不同值。
    if (ok) {
        Property targetDesc;
        bool targetHas = false;
        if (Value* e = targetOwnDescriptor(ctx, d->target, key, targetHas, targetDesc)) {
            return e;
        }
        if (targetHas && !isAccessorDescriptor(targetDesc) && !targetDesc.configurable &&
            !targetDesc.writable) {
            Value* tv = dataValueOrUndefined(ctx, targetDesc);
            if (!detail::sameValue(value, tv)) {
                return detail::typeError(
                    ctx, "proxy set trap cannot change a non-writable, non-configurable property");
            }
        }
    }
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

Value* ProxyType::hasProperty(IContext* ctx, Value* self, Value* key) {
    auto* d = asProxy(self);
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "has", present, err);
    if (err != nullptr) return err;
    if (!present) return ordinaryHas(ctx, d->target, pk);
    Value* argv[2] = {d->target, pk.value};
    Value* r = ctx->call(trap, d->handler, 2, argv);
    if (detail::isErrorValue(r)) return r;

    // —— 不变量（ES2024 §10.5.7）——
    bool result = detail::toBoolean(r);
    if (!result) {
        Property targetDesc;
        bool targetHas = false;
        if (Value* e = targetOwnDescriptor(ctx, d->target, pk, targetHas, targetDesc)) return e;
        if (!targetHas) {
            Value* ext = targetExtensible(ctx, d->target);
            if (detail::isErrorValue(ext)) return ext;
            if (!detail::toBoolean(ext)) {
                return detail::typeError(
                    ctx,
                    "proxy has trap cannot report a missing property on a non-extensible target");
            }
        } else if (!targetDesc.configurable) {
            return detail::typeError(
                ctx, "proxy has trap cannot hide a non-configurable property");
        }
    }
    return ctx->createBoolean(result);
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
    bool deleted = detail::toBoolean(r);
    if (!deleted) return ctx->createBoolean(false);

    // —— 不变量（ES2024 §10.5.10）——
    Property targetDesc;
    bool targetHas = false;
    if (Value* e = targetOwnDescriptor(ctx, d->target, pk, targetHas, targetDesc)) return e;
    if (targetHas && !targetDesc.configurable) {
        return detail::typeError(
            ctx, "proxy deleteProperty trap cannot delete a non-configurable property");
    }
    return ctx->createBoolean(true);
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
    Value* descObj = propertyDescriptorToValue(ctx, desc);
    if (detail::isErrorValue(descObj)) return descObj;
    Value* argv[3] = {d->target, pk.value, descObj};
    Value* r = ctx->call(trap, d->handler, 3, argv);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) return ctx->createBoolean(false);
    if (Value* e = checkDefineInvariant(ctx, d->target, pk, desc)) return e;
    return ctx->createBoolean(true);
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
    Value* g = (getter != nullptr && detail::isCallable(getter)) ? getter : nullptr;
    Value* s = (setter != nullptr && detail::isCallable(setter)) ? setter : nullptr;
    Property desc{nullptr, false, enumerable, configurable, g, s};
    Value* descObj = propertyDescriptorToValue(ctx, desc);
    if (detail::isErrorValue(descObj)) return descObj;
    Value* argv[3] = {d->target, pk.value, descObj};
    Value* r = ctx->call(trap, d->handler, 3, argv);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) return ctx->createBoolean(false);
    if (Value* e = checkDefineInvariant(ctx, d->target, pk, desc)) return e;
    return ctx->createBoolean(true);
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

    // —— 不变量（ES2024 §10.5.2）——
    Value* ext = targetExtensible(ctx, d->target);
    if (detail::isErrorValue(ext)) return ext;
    if (!detail::toBoolean(ext)) {
        Value* targetProto = d->target->type()->getPrototype(ctx, d->target);
        if (detail::isErrorValue(targetProto)) return targetProto;
        if (!detail::sameValue(r, targetProto)) {
            return detail::typeError(
                ctx, "proxy getPrototypeOf trap returned a different prototype for a non-extensible target");
        }
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
    if (!detail::toBoolean(r)) return ctx->createBoolean(false);

    // —— 不变量（ES2024 §10.5.3）——
    Value* ext = targetExtensible(ctx, d->target);
    if (detail::isErrorValue(ext)) return ext;
    if (!detail::toBoolean(ext)) {
        Value* targetProto = d->target->type()->getPrototype(ctx, d->target);
        if (detail::isErrorValue(targetProto)) return targetProto;
        if (!detail::sameValue(proto, targetProto)) {
            return detail::typeError(
                ctx, "proxy setPrototypeOf trap cannot change the prototype of a non-extensible target");
        }
    }
    return ctx->createBoolean(true);
}

Value* ProxyType::isExtensible(IContext* ctx, Value* self) {
    auto* d = asProxy(self);
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "isExtensible", present, err);
    if (err != nullptr) return err;
    if (!present) return d->target->type()->isExtensible(ctx, d->target);

    Value* argv[1] = {d->target};
    Value* r = ctx->call(trap, d->handler, 1, argv);
    if (detail::isErrorValue(r)) return r;
    bool trapResult = detail::toBoolean(r);

    // —— 不变量（ES2024 §10.5.4）——
    Value* targetResult = d->target->type()->isExtensible(ctx, d->target);
    if (detail::isErrorValue(targetResult)) return targetResult;
    if (trapResult != detail::toBoolean(targetResult)) {
        return detail::typeError(
            ctx, "proxy isExtensible trap result does not match target extensibility");
    }
    return ctx->createBoolean(trapResult);
}

Value* ProxyType::preventExtensions(IContext* ctx, Value* self) {
    auto* d = asProxy(self);
    bool present = false;
    Value* err = nullptr;
    Value* trap = lookupTrap(ctx, d, "preventExtensions", present, err);
    if (err != nullptr) return err;
    if (!present) return d->target->type()->preventExtensions(ctx, d->target);

    Value* argv[1] = {d->target};
    Value* r = ctx->call(trap, d->handler, 1, argv);
    if (detail::isErrorValue(r)) return r;
    bool trapResult = detail::toBoolean(r);

    // —— 不变量（ES2024 §10.5.6）——
    if (trapResult) {
        Value* targetResult = d->target->type()->isExtensible(ctx, d->target);
        if (detail::isErrorValue(targetResult)) return targetResult;
        if (detail::toBoolean(targetResult)) {
            return detail::typeError(
                ctx, "proxy preventExtensions trap returned true but target is still extensible");
        }
    }
    return ctx->createBoolean(trapResult);
}

}  // namespace neo::core
