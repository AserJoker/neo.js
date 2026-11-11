// ProxyBuiltins.cpp - Proxy 构造函数与 Proxy.revocable（ES2024 §28.2）
//
// 关联 ADR: 020（标准库组织）、048（Proxy 内部方法层）

#include "core/ProxyBuiltins.h"

#include "core/IContext.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Proxy.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// new Proxy(target, handler)（ES2024 §28.2.1.1）
Value* proxyCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    // Proxy 只能通过 new 调用（§28.2.1.1 step 1）。
    if (ctx->newTarget() == nullptr) {
        return detail::typeError(ctx, "Constructor Proxy requires 'new'");
    }
    if (argc < 2) {
        return detail::typeError(ctx, "Proxy constructor requires target and handler");
    }
    return ctx->createProxy(argv[0], argv[1]);
}

// revoke()（Proxy.revocable 返回的撤销函数）：把 [[ProxyHandler]] 置 null。
Value* proxyRevoke(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    // revoke 函数把待撤销的 proxy 存于 [[HomeObject]]（内部实现细节）。
    Value* proxy = ctx->functionHomeObject(ctx->currentFunction());
    revokeProxy(proxy);
    return ctx->createUndefined();
}

// Proxy.revocable(target, handler)（ES2024 §28.2.2.1）
Value* proxyRevocable(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc < 2) {
        return detail::typeError(ctx, "Proxy.revocable requires target and handler");
    }
    Value* proxy = ctx->createProxy(argv[0], argv[1]);
    if (detail::isErrorValue(proxy)) return proxy;

    FunctionCreateOptions opts;
    opts.hasPrototypeProperty = false;
    opts.homeObject = proxy;  // revoke 闭包捕获（GC 由 FunctionData::enumerateChildren 遍历）
    Value* revoke = ctx->createNativeFunction(proxyRevoke, 0, {}, opts);
    if (revoke == ctx->oomValue()) return revoke;
    ctx->setFunctionName(revoke, "");

    Value* result = ctx->createObject();
    if (result == ctx->oomValue()) return result;
    Value* r = ctx->defineProperty(result, ctx->createString("proxy"), proxy, true, true,
                                   true);
    if (detail::isErrorValue(r)) return r;
    r = ctx->defineProperty(result, ctx->createString("revoke"), revoke, true, true, true);
    if (detail::isErrorValue(r)) return r;
    return result;
}

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerProxyBuiltins(IContext* ctx) {
    // Proxy 构造函数没有 prototype 属性（ES2024 §28.2.1）。
    FunctionCreateOptions opts;
    opts.hasPrototypeProperty = false;
    Value* proxyFn = ctx->createNativeFunction(proxyCtor, 2, {}, opts);
    if (proxyFn == ctx->oomValue()) return false;
    ctx->setFunctionName(proxyFn, "Proxy");

    if (!defineMethod(ctx, proxyFn, "revocable", proxyRevocable, 2)) return false;

    ctx->global()->add(proxyFn);
    ctx->global()->bind("Proxy", proxyFn);
    return true;
}

}  // namespace neo::core
