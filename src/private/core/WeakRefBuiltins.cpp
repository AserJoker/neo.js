// WeakRefBuiltins.cpp - WeakRef 注册实现（ES2024 §26.1）
//
// WeakRef(target)：
//   - 仅 new 调用；target 须可被弱持有（对象或未注册 symbol）
//   - 实例为普通对象 + [[Target]] 内部槽
// WeakRef.prototype.deref()：返回 [[Target]]（当前以强引用保存，永不清空）。
//
// 关联 ADR: 020（标准库组织）、016（Type 体系）

#include "core/WeakRefBuiltins.h"

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Proxy.h"
#include "core/types/TypeBehavior.h"
#include "core/types/WeakRef.h"

#include <string>

namespace neo::core {

WeakRefData* asWeakRef(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::WeakRef) return nullptr;
    return static_cast<WeakRefData*>(v->data());
}

void WeakRefData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (target != nullptr) visitor->visit(target);
}

IData* WeakRefData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<WeakRefData*>(this);
    self->refInc();
    return self;
}

namespace {

// CanBeHeldWeakly（ES2024 §7.2.11）：对象或未注册 symbol。
bool canBeHeldWeakly(IContext* ctx, Value* v) {
    const TypeKind k = v->type()->kind();
    if (detail::isObjectKind(k)) return true;
    if (k != TypeKind::Symbol) return false;
    Value* key = ctx->symbolKeyFor(v);
    return !detail::isErrorValue(key) &&
           key->type()->kind() == TypeKind::Undefined;
}

// WeakRef(target)：仅 new 调用。
Value* weakRefCtor(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                   Value* /*argv*/[]) {
    return detail::typeError(ctx, "Constructor WeakRef requires 'new'");
}

Value* weakRefConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                        Value* argv[]) {
    Value* target = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!canBeHeldWeakly(ctx, target)) {
        return detail::typeError(ctx, "Invalid value used in weak ref");
    }
    // GetPrototypeFromConstructor(newTarget, %WeakRefPrototype%)。
    Value* newTarget = ctx->newTarget();
    Value* proto = ctx->getProperty(newTarget, "prototype");
    if (detail::isErrorValue(proto)) return proto;
    if (!detail::isObjectKind(proto->type()->kind())) {
        // 单 realm：仅已撤销 Proxy 会使 GetFunctionRealm 失败。
        Value* cur = newTarget;
        while (cur != nullptr && cur->type()->kind() == TypeKind::Function) {
            auto* d = static_cast<FunctionData*>(cur->data());
            if (d->boundTarget == nullptr) break;
            cur = d->boundTarget;
        }
        if (cur != nullptr && cur->type()->kind() == TypeKind::Proxy &&
            proxyHandler(cur) == nullptr) {
            return detail::typeError(
                ctx, "Cannot get function realm from revoked Proxy");
        }
        Value* weakRefProto = ctx->getProperty(
            ctx->getProperty(ctx->globalObject(), "WeakRef"), "prototype");
        if (detail::isErrorValue(weakRefProto)) return weakRefProto;
        proto = weakRefProto;
    }
    Value* ref = ctx->createWeakRef(target);
    if (detail::isErrorValue(ref)) return ref;
    Value* r = ctx->setPrototype(ref, proto);
    if (detail::isErrorValue(r)) return r;
    return ref;
}

Value* weakRefDeref(IContext* ctx, Value* self, size_t /*argc*/,
                    Value* /*argv*/[]) {
    WeakRefData* d = asWeakRef(self);
    if (d == nullptr) {
        return detail::typeError(
            ctx, "WeakRef.prototype.deref called on incompatible receiver");
    }
    return d->target != nullptr ? d->target : ctx->createUndefined();
}

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t length) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(fn, 0, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    if (ctx->defineProperty(f, "length",
                            ctx->createNumber(static_cast<double>(length)), false,
                            false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return ctx->defineProperty(target, name, f, true, false, true)
               ->type()->kind() != TypeKind::Exception;
}

}  // namespace

bool registerWeakRefBuiltins(IContext* ctx) {
    Value* proto = ctx->createObject();
    if (proto == ctx->oomValue()) return false;

    Value* ctor = ctx->createNativeFunction(weakRefCtor, 0);
    if (ctor == ctx->oomValue()) return false;
    ctx->setFunctionName(ctor, "WeakRef");
    ctx->setFunctionConstructor(ctor, weakRefConstruct);
    if (ctx->defineProperty(ctor, "length", ctx->createNumber(1.0), false, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(ctor, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(proto, "constructor", ctor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, proto, "deref", weakRefDeref, 0)) return false;
    if (ctx->defineProperty(proto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString("WeakRef"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    ctx->setWeakRefPrototype(proto);
    return ctx->defineGlobal("WeakRef", ctor, true, false, true);
}

}  // namespace neo::core
