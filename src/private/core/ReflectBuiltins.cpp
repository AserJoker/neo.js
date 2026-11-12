// ReflectBuiltins.cpp - Reflect 命名空间（ES2024 §28.1）
//
// Reflect 的每个方法都是对象内部方法的薄封装：
//   - target 必须是对象（否则 TypeError）；
//   - 属性键走 ToPropertyKey，描述符走 ToPropertyDescriptor；
//   - get/set 支持 receiver 参数，经逐跳虚分派（原型链上的 Proxy 会触发 trap）；
//   - defineProperty / setPrototypeOf 返回 Boolean 而非抛错（trap 抛出的异常仍透传）。
//
// 关联 ADR: 020（标准库组织）、048（Proxy 内部方法层）

#include "core/ReflectBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Object.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <vector>

namespace neo::core {

namespace {

// Reflect 方法的 target 必须是对象。
Value* requireObject(IContext* ctx, Value* v, const char* method) {
    if (v != nullptr && detail::isObjectKind(v->type()->kind())) return v;
    return detail::typeError(ctx, std::string("Reflect.") + method +
                                      " called on non-object");
}

// CreateListFromArrayLike（ES2024 §7.3.17）：只接受对象，读取 length 与索引。
Value* listFromArrayLike(IContext* ctx, Value* v, const char* method,
                         std::vector<Value*>& out) {
    if (v == nullptr || !detail::isObjectKind(v->type()->kind())) {
        return detail::typeError(ctx, std::string("Reflect.") + method +
                                          ": argumentsList must be an object");
    }
    uint32_t len = 0;
    if (Value* err = detail::lengthOf(ctx, v, len)) return err;
    out.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
        Value* item = ctx->getProperty(v, ctx->createString(std::to_string(i)));
        if (detail::isErrorValue(item)) return item;
        out.push_back(item);
    }
    return nullptr;
}

Value* keysToArray(IContext* ctx, const std::vector<Value*>& keys) {
    Value* arr = ctx->createArray();
    if (arr == ctx->oomValue()) return arr;
    auto* d = static_cast<ArrayData*>(arr->data());
    for (size_t i = 0; i < keys.size(); ++i) {
        d->setElement(static_cast<uint32_t>(i), keys[i]);
    }
    return arr;
}

// Reflect.apply(target, thisArgument, argumentsList)
Value* reflectApply(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(target)) {
        return detail::typeError(ctx, "Reflect.apply target is not a function");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* argsList = argc > 2 ? argv[2] : ctx->createUndefined();
    std::vector<Value*> args;
    if (Value* err = listFromArrayLike(ctx, argsList, "apply", args)) return err;
    return ctx->call(target, thisArg, args.size(), args.empty() ? nullptr : args.data());
}

// Reflect.construct(target, argumentsList [, newTarget])
Value* reflectConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isConstructor(target)) {
        return detail::typeError(ctx, "Reflect.construct target is not a constructor");
    }
    Value* argsList = argc > 1 ? argv[1] : ctx->createUndefined();
    std::vector<Value*> args;
    if (Value* err = listFromArrayLike(ctx, argsList, "construct", args)) return err;
    Value* newTarget = argc > 2 ? argv[2] : target;
    if (!detail::isConstructor(newTarget)) {
        return detail::typeError(ctx, "Reflect.construct newTarget is not a constructor");
    }
    return ctx->construct(target, newTarget, args.size(),
                          args.empty() ? nullptr : args.data());
}

// Reflect.defineProperty(target, propertyKey, attributes)
Value* reflectDefineProperty(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "defineProperty");
    if (detail::isErrorValue(target)) return target;
    PropertyKey key;
    if (Value* err = toPropertyKey(ctx, argc > 1 ? argv[1] : ctx->createUndefined(), key)) {
        return err;
    }
    Value* descObj = argc > 2 ? argv[2] : ctx->createUndefined();
    if (descObj == nullptr || !detail::isObjectKind(descObj->type()->kind())) {
        return detail::typeError(ctx, "Reflect.defineProperty: descriptor must be an object");
    }
    PropertyDescriptor desc;
    if (Value* err = partialPropertyDescriptorFromValue(ctx, descObj, desc)) return err;
    Value* r = definePropertyFromPartialDescriptor(ctx, target, key, desc);
    // 内部方法以 Value 表达三态：Exception = 抛出（透传），Boolean = 操作结果。
    if (detail::isErrorValue(r)) return r;
    return ctx->createBoolean(detail::toBoolean(r));
}

// Reflect.deleteProperty(target, propertyKey)
Value* reflectDeleteProperty(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "deleteProperty");
    if (detail::isErrorValue(target)) return target;
    Value* key = argc > 1 ? argv[1] : ctx->createUndefined();
    return ctx->deleteProperty(target, key);
}

// Reflect.get(target, propertyKey [, receiver])
Value* reflectGet(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "get");
    if (detail::isErrorValue(target)) return target;
    PropertyKey key;
    if (Value* err = toPropertyKey(ctx, argc > 1 ? argv[1] : ctx->createUndefined(), key)) {
        return err;
    }
    Value* receiver = argc > 2 ? argv[2] : target;
    return target->type()->getWithReceiver(ctx, target, key, receiver);
}

// Reflect.getOwnPropertyDescriptor(target, propertyKey)
Value* reflectGetOwnPropertyDescriptor(IContext* ctx, Value* /*self*/, size_t argc,
                                       Value* argv[]) {
    Value* target =
        requireObject(ctx, argc > 0 ? argv[0] : nullptr, "getOwnPropertyDescriptor");
    if (detail::isErrorValue(target)) return target;
    PropertyKey key;
    if (Value* err = toPropertyKey(ctx, argc > 1 ? argv[1] : ctx->createUndefined(), key)) {
        return err;
    }
    bool exists = false;
    Property prop;
    if (Value* err = target->type()->getOwnProperty(ctx, target, key, exists, prop)) {
        return err;
    }
    if (!exists) return ctx->createUndefined();
    return propertyDescriptorToValue(ctx, prop);
}

// Reflect.getPrototypeOf(target)
Value* reflectGetPrototypeOf(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "getPrototypeOf");
    if (detail::isErrorValue(target)) return target;
    return ctx->getPrototype(target);
}

// Reflect.has(target, propertyKey)
Value* reflectHas(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "has");
    if (detail::isErrorValue(target)) return target;
    Value* key = argc > 1 ? argv[1] : ctx->createUndefined();
    return ctx->hasProperty(target, key);
}

// Reflect.isExtensible(target)
Value* reflectIsExtensible(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "isExtensible");
    if (detail::isErrorValue(target)) return target;
    return target->type()->isExtensible(ctx, target);
}

// Reflect.ownKeys(target)
Value* reflectOwnKeys(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "ownKeys");
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, false, true, keys)) return err;
    return keysToArray(ctx, keys);
}

// Reflect.preventExtensions(target)
Value* reflectPreventExtensions(IContext* ctx, Value* /*self*/, size_t argc,
                                Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "preventExtensions");
    if (detail::isErrorValue(target)) return target;
    return target->type()->preventExtensions(ctx, target);
}

// Reflect.set(target, propertyKey, V [, receiver])
Value* reflectSet(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "set");
    if (detail::isErrorValue(target)) return target;
    PropertyKey key;
    if (Value* err = toPropertyKey(ctx, argc > 1 ? argv[1] : ctx->createUndefined(), key)) {
        return err;
    }
    Value* value = argc > 2 ? argv[2] : ctx->createUndefined();
    Value* receiver = argc > 3 ? argv[3] : target;
    Value* r = target->type()->setWithReceiver(ctx, target, key, value, receiver);
    if (detail::isErrorValue(r)) return r;
    return ctx->createBoolean(detail::toBoolean(r));
}

// Reflect.setPrototypeOf(target, proto)
Value* reflectSetPrototypeOf(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = requireObject(ctx, argc > 0 ? argv[0] : nullptr, "setPrototypeOf");
    if (detail::isErrorValue(target)) return target;
    Value* proto = argc > 1 ? argv[1] : ctx->createUndefined();
    TypeKind pk = proto->type()->kind();
    if (pk != TypeKind::Null && !detail::isObjectKind(pk)) {
        return detail::typeError(ctx, "Reflect.setPrototypeOf: proto must be an object or null");
    }
    Value* r = target->type()->setPrototype(ctx, target, proto);
    if (detail::isErrorValue(r)) return r;
    return ctx->createBoolean(detail::toBoolean(r));
}

// 定义命名空间方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerReflectBuiltins(IContext* ctx) {
    Value* reflect = ctx->createObject();
    if (reflect == ctx->oomValue()) return false;

    if (!defineMethod(ctx, reflect, "apply", reflectApply, 3)) return false;
    if (!defineMethod(ctx, reflect, "construct", reflectConstruct, 2)) return false;
    if (!defineMethod(ctx, reflect, "defineProperty", reflectDefineProperty, 3)) return false;
    if (!defineMethod(ctx, reflect, "deleteProperty", reflectDeleteProperty, 2)) return false;
    if (!defineMethod(ctx, reflect, "get", reflectGet, 2)) return false;
    if (!defineMethod(ctx, reflect, "getOwnPropertyDescriptor",
                      reflectGetOwnPropertyDescriptor, 2))
        return false;
    if (!defineMethod(ctx, reflect, "getPrototypeOf", reflectGetPrototypeOf, 1)) return false;
    if (!defineMethod(ctx, reflect, "has", reflectHas, 2)) return false;
    if (!defineMethod(ctx, reflect, "isExtensible", reflectIsExtensible, 1)) return false;
    if (!defineMethod(ctx, reflect, "ownKeys", reflectOwnKeys, 1)) return false;
    if (!defineMethod(ctx, reflect, "preventExtensions", reflectPreventExtensions, 1))
        return false;
    if (!defineMethod(ctx, reflect, "set", reflectSet, 3)) return false;
    if (!defineMethod(ctx, reflect, "setPrototypeOf", reflectSetPrototypeOf, 2)) return false;

    // Reflect[@@toStringTag] = "Reflect"（不可写/不可枚举/可配置）
    Value* tag = ctx->createString("Reflect");
    if (tag == ctx->oomValue()) return false;
    if (ctx->defineProperty(reflect, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            tag, false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    ctx->global()->add(reflect);
    ctx->global()->bind("Reflect", reflect);
    return true;
}

}  // namespace neo::core
