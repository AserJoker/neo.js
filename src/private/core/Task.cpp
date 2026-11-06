// Task.cpp - 事件循环任务执行器实现
//
// defaultTaskRunner 是任务队列的默认执行器：把 entity 当函数无参调用，
// 并把执行结果按 ADR-006/017 §4 分流：
//   - Fatal：原样透传（不可捕获，交给宿主）；
//   - Exception：打印 "Uncaught <消息>" 后返回 undefined（任务失败不终止进程）；
//   - 其他：返回函数返回值。
//
// 当前阶段没有 Error 对象，异常携带的 child 通常是 String，直接取其内容打印。
//
// 关联 ADR: 006（异常即值）、017 §4（Fatal 透传）、020（标准库组织）

#include "core/Task.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Exception.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// 取异常携带的错误消息（ADR-039）。
// child 通常是 Error 对象（引擎内部报错）或 String（用户/测试直接构造）；
// Error 对象按其 toString 渲染（"Name: message"，ES2024 §20.5.3.4）。
std::string exceptionMessage(IContext* ctx, Value* err) {
    // ExceptionData::child 契约上永不为 nullptr（无错误对象时是 undefined Value）
    Value* child = static_cast<ExceptionData*>(err->data())->child;
    if (child->type()->kind() == TypeKind::Error) {
        Value* toString = ctx->getProperty(child, "toString");
        if (!detail::isErrorValue(toString) && detail::isCallable(toString)) {
            Value* s = ctx->call(toString, child, 0, nullptr);
            if (!detail::isErrorValue(s) && s->type()->kind() == TypeKind::String) {
                return static_cast<StringData*>(s->data())->value;
            }
        }
    }
    if (child->type()->kind() == TypeKind::String) {
        return static_cast<StringData*>(child->data())->value;
    }
    return detail::valueToString(child);
}

// 打印未捕获异常并返回 undefined（任务失败不终止进程）。
// 只接收 Exception：Fatal 由 defaultTaskRunner 直接透传，不进入此函数。
Value* reportUncaught(IContext* ctx, Value* err) {
    ctx->console()->printLine("Uncaught {}", exceptionMessage(ctx, err));
    return ctx->createUndefined();
}

}  // namespace

Value* defaultTaskRunner(IContext* ctx, Value* task) {
    // 任务实体必须是函数（当前阶段唯一的可执行实体）
    if (task == nullptr || task->type()->kind() != TypeKind::Function) {
        Value* err = detail::typeError(ctx, "Task entity is not a function");
        return reportUncaught(ctx, err);
    }
    // 以 undefined 为 this 无参调用；返回值走统一异常通道（永不为 nullptr）
    Value* result = ctx->call(task, ctx->createUndefined(), 0, nullptr);
    if (result->type()->kind() == TypeKind::Exception) {
        return reportUncaught(ctx, result);
    }
    return result;  // 正常值或 Fatal（Fatal 由调用方透传）
}

}  // namespace neo::core
