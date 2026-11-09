// PromiseBuiltins.cpp - Promise 构造函数与 %Promise.prototype% 注册实现
//
// M1 只建立类型骨架：原型对象、构造函数、@@toStringTag、双向 constructor。
// then/catch/finally 与静态方法在 M2/M3 加入。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）、043（Promise）

#include "core/PromiseBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Promise.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

namespace {

// Promise(executor) 不带 new 调用 → TypeError（ES2024 §27.2.3.1 step 1）。
Value* promiseCtor(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return detail::typeError(ctx, "Promise constructor cannot be invoked without 'new'");
}

// new Promise(executor)（ES2024 §27.2.3.1）：
//   executor 必须可调用，否则 TypeError；
//   同步调用 executor(resolve, reject)；executor 抛错 → reject。
// 本里程碑只建对象；resolving functions 在 M2 接入。
Value* promiseConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc < 1 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "Promise resolver is not a function");
    }
    return ctx->createPromise();
}

// 把值挂到 global scope：add 跟踪 + bind 名字
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->global()->add(v);
    ctx->global()->bind(name, v);
}

}  // namespace

bool registerPromiseBuiltins(IContext* ctx) {
    // —— 1. Promise.prototype（普通对象，原型 = Object.prototype）——
    // 此时 promisePrototype_ 尚未赋值，createPromise 不会自动挂链。
    Value* promiseProto = ctx->createObject();
    if (promiseProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(promiseProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    // 原型就绪：后续 createPromise 自动挂链
    ctx->setPromisePrototype(promiseProto);

    // —— 2. Promise 构造函数 ——
    Value* promiseFn = ctx->createNativeFunction(promiseCtor, 1);
    if (promiseFn == ctx->oomValue()) return false;
    ctx->setFunctionName(promiseFn, "Promise");
    // call/construct 语义不同：new 走专用钩子（§27.2.3.1 step 1 要求无 new 报错）
    ctx->setFunctionConstructor(promiseFn, promiseConstruct);
    // Promise.prototype 属性指向 promiseProto（不可写/不可枚举/不可配置，§27.2.4.6）
    if (ctx->defineProperty(promiseFn, "prototype", promiseProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // promiseProto.constructor = Promise
    if (ctx->defineProperty(promiseProto, "constructor", promiseFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 3. @@toStringTag = "Promise"（§27.2.5.5，不可写/不可枚举/可配置）——
    if (ctx->defineProperty(promiseProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString("Promise"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 4. 注册到 global ——
    bindGlobal(ctx, "Promise", promiseFn);
    return true;
}

}  // namespace neo::core
