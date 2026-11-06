// TimerBuiltins.cpp - setTimeout / setInterval / clearTimeout / clearInterval 实现
//
// 定时器不新增调度机制，只是事件循环的宿主侧薄封装：
//   setTimeout  → postTask（一次性宏任务）
//   setInterval → postLoopTask（循环宏任务）
//   clear*      → cancelTask（两个 clear 等价，按浏览器语义可互换）
//
// 回调与额外参数：Task 的实体必须是函数，故有额外参数时先用 createBoundFunction
// 把参数前置绑定（this = undefined），再作为任务实体入队。无额外参数时直接用原函数，
// 避免多分配一个绑定函数。
//
// 延迟按 HTML Timers 语义做 ToNumber 并钳到非负整数；NaN/负数 → 0。
//
// 关联 ADR: 020（标准库组织）、006（异常即值）、017 §4（Fatal 透传）

#include "core/TimerBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <string>

namespace neo::core {

namespace {

// 定时器延迟上限（HTML Timers：2^31-1 ms，约 24.8 天）
inline constexpr uint64_t kMaxTimerDelay = 2147483647ull;

// 延迟参数 → 毫秒（HTML Timers §8.6：ToNumber 后取非负整数，NaN/负数 → 0，
// 超过 2^31-1 钳到 2^31-1）。转换失败（如 Symbol）返回 Exception/Fatal，由调用方透传。
Value* delayOf(IContext* ctx, size_t argc, Value* argv[], uint64_t& out) {
    out = 0;
    if (argc < 2) return nullptr;  // 缺省 delay = 0
    Value* nv = detail::toNumberValue(ctx, argv[1]);
    if (detail::isErrorValue(nv)) return nv;
    double n = static_cast<NumberData*>(nv->data())->value;
    if (std::isnan(n) || n <= 0.0) {
        out = 0;
    } else if (n > static_cast<double>(kMaxTimerDelay)) {
        out = kMaxTimerDelay;
    } else {
        out = static_cast<uint64_t>(n);
    }
    return nullptr;
}

// 取第一个参数作为回调，校验可调用；有额外参数时绑定成新函数。
// 失败返回 Exception/Fatal（调用方透传）。
Value* callbackOf(IContext* ctx, size_t argc, Value* argv[], Value*& out) {
    if (argc < 1 || argv[0]->type()->kind() != TypeKind::Function) {
        return detail::typeError(ctx, "Timer callback must be a function");
    }
    if (argc <= 2) {
        out = argv[0];  // 无额外参数：直接用原函数
        return nullptr;
    }
    // 额外参数前置绑定（this = undefined）
    out = ctx->createBoundFunction(argv[0], ctx->createUndefined(), argc - 2, argv + 2);
    if (detail::isErrorValue(out)) return out;
    return nullptr;
}

// setTimeout(fn, delay, ...args)
Value* timerSetTimeout(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    uint64_t delay = 0;
    if (Value* err = delayOf(ctx, argc, argv, delay)) return err;
    Value* callback = nullptr;
    if (Value* err = callbackOf(ctx, argc, argv, callback)) return err;
    return ctx->createNumber(static_cast<double>(ctx->postTask(callback, delay)));
}

// setInterval(fn, delay, ...args)
Value* timerSetInterval(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    uint64_t delay = 0;
    if (Value* err = delayOf(ctx, argc, argv, delay)) return err;
    Value* callback = nullptr;
    if (Value* err = callbackOf(ctx, argc, argv, callback)) return err;
    return ctx->createNumber(static_cast<double>(ctx->postLoopTask(callback, delay)));
}

// clearTimeout(id) / clearInterval(id)：id 非数字或未知时静默忽略（浏览器语义）。
Value* timerClear(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc < 1) return ctx->createUndefined();
    Value* nv = detail::toNumberValue(ctx, argv[0]);
    if (detail::isErrorValue(nv)) return nv;  // Symbol 等无法转数字：透传
    double n = static_cast<NumberData*>(nv->data())->value;
    if (!std::isnan(n) && n >= 1.0 && n <= 9007199254740991.0) {
        ctx->cancelTask(static_cast<uint64_t>(n));
    }
    return ctx->createUndefined();
}

// 注册一个全局函数（writable / 不可枚举 / 可配置，与浏览器一致）
bool bindGlobalFn(IContext* ctx, const char* name, NativeFn fn, size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    ctx->global()->add(f);
    ctx->global()->bind(name, f);
    return true;
}

}  // namespace

bool registerTimerBuiltins(IContext* ctx) {
    bool ok = true;
    ok &= bindGlobalFn(ctx, "setTimeout", timerSetTimeout, 1);
    ok &= bindGlobalFn(ctx, "setInterval", timerSetInterval, 1);
    // clear* 的 id 可缺省（浏览器语义：无参调用返回 undefined，不报错）
    ok &= bindGlobalFn(ctx, "clearTimeout", timerClear, 0);
    ok &= bindGlobalFn(ctx, "clearInterval", timerClear, 0);
    return ok;
}

}  // namespace neo::core
