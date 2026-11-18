// ConsoleBuiltins.cpp - 宿主侧 console 与 queueMicrotask
//
// 设计要点：
//   - console.* 用 ToString 逐个格式化参数并以空格连接（不做 util.inspect 式展开）；
//   - 输出统一走注入的 infra::Console；console.error/warn 暂不区分流；
//   - queueMicrotask 直接把回调入 microtask 队列（§8.4）。
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#include "core/ConsoleBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"

#include <string>

namespace neo::core {

namespace {

// 把一个参数转成可打印文本；转换失败时退化为类型名，避免 console 自身抛错。
std::string formatArg(IContext* ctx, Value* v) {
    Value* s = ctx->toString(v);
    if (detail::isErrorValue(s)) {
        return std::string("<") + v->type()->name() + ">";
    }
    TypeKind k = s->type()->kind();
    if (k != TypeKind::String) return "<unprintable>";
    return static_cast<StringData*>(s->data())->value;
}

std::string joinArgs(IContext* ctx, size_t argc, Value* argv[]) {
    std::string out;
    for (size_t i = 0; i < argc; ++i) {
        if (i != 0) out += ' ';
        out += formatArg(ctx, argv[i]);
    }
    return out;
}

Value* consoleWrite(IContext* ctx, size_t argc, Value* argv[]) {
    ctx->console()->writeLine(joinArgs(ctx, argc, argv));
    return ctx->createUndefined();
}

Value* consoleLog(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return consoleWrite(ctx, argc, argv);
}

Value* consoleAssert(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc > 0 && detail::toBoolean(argv[0])) return ctx->createUndefined();
    std::string text = "Assertion failed";
    if (argc > 1) {
        text += ": ";
        text += joinArgs(ctx, argc - 1, argv + 1);
    }
    ctx->console()->writeLine(text);
    return ctx->createUndefined();
}

Value* queueMicrotaskFn(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* callback = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(callback)) {
        return detail::typeError(ctx, "queueMicrotask callback must be a function");
    }
    ctx->postMicrotask(callback);
    return ctx->createUndefined();
}

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, true, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerConsoleBuiltins(IContext* ctx) {
    Value* console = ctx->createObject();
    if (console == ctx->oomValue()) return false;
    for (const char* name : {"log", "info", "warn", "error", "debug"}) {
        if (!defineMethod(ctx, console, name, consoleLog, 0)) return false;
    }
    if (!defineMethod(ctx, console, "assert", consoleAssert, 0)) return false;
    ctx->defineGlobal("console", console, true, false, true);

    Value* queue = ctx->createNativeFunction(queueMicrotaskFn, 1);
    if (queue == ctx->oomValue()) return false;
    ctx->setFunctionName(queue, "queueMicrotask");
    ctx->defineGlobal("queueMicrotask", queue, true, false, true);
    return true;
}

}  // namespace neo::core
