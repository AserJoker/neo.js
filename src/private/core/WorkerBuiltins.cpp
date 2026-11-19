// WorkerBuiltins.cpp - JS Worker / postMessage / onmessage
//
// 主线程：new Worker(path) 读取脚本源码，启动原生 worker（独立线程 + Context）。
// worker 线程：setup 注册 postMessage/onmessage 并执行入口脚本；之后每条 inbox
// 消息作为一次宏任务调用 onmessage({data})。
//
// 主线程侧：worker 脚本调用 postMessage 时，把 MessageValue 推入 outbox，并通过
// Context::postExternalTask 唤醒主线程事件循环；外部任务在 owner 线程把消息
// 还原成 Value 并调用 Worker 对象的 onmessage。
//
// 线程边界：消息只以 MessageValue 传递；Value/Context 不跨线程。
//
// 关联 ADR: 006（异常即值）、020（标准库组织）、028（infra）

#include "core/WorkerBuiltins.h"

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "core/types/Worker.h"
#include "frontend/Parser.h"
#include "infra/IFileSystem.h"
#include "sema/Sema.h"
#include "vm/VM.h"
#include "worker/IWorker.h"
#include "worker/MessageClone.h"

#include <memory>
#include <string>
#include <utility>

namespace neo::core {

namespace {

using worker::IWorker;
using worker::Message;
using worker::MessageQueue;
using worker::MessageValue;
using worker::WorkerCallbacks;
using worker::WorkerOptions;

// worker 线程专属环境（每线程一个 worker，thread_local 即天然隔离）。
struct WorkerThreadEnv {
    MessageQueue* outbox = nullptr;
    IContext* mainCtx = nullptr;
    std::shared_ptr<WorkerState> state;
};

thread_local WorkerThreadEnv t_env;

void reportWorkerError(IContext* ctx, const std::string& message) {
    if (ctx->console() != nullptr) {
        ctx->console()->printLine("Worker: {}", message);
    }
}

// 主线程：把 outbox 中的消息逐条派发给 Worker 对象的 onmessage。
void dispatchToMain(IContext* ctx, WorkerState* state) {
    if (!state->alive.load(std::memory_order_acquire) || state->worker == nullptr) {
        return;
    }
    Value* workerObject = state->workerObject;
    if (workerObject == nullptr) return;

    Message msg;
    while (state->worker->tryReceive(msg)) {
        Value* handler = ctx->getProperty(workerObject, "onmessage");
        if (!detail::isCallable(handler)) continue;

        Value* data = worker::messageToValue(ctx, msg.value);
        if (detail::isErrorValue(data)) continue;

        Value* event = ctx->createObject();
        if (event == ctx->oomValue()) return;
        ctx->defineProperty(event, "data", data);
        (void)ctx->call(handler, workerObject, 1, &event);
    }
}

// worker 线程：全局 postMessage(value)。
Value* workerPostMessageGlobal(IContext* ctx, Value* /*self*/, size_t argc,
                               Value* argv[]) {
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    MessageValue mv;
    if (Value* err = worker::valueToMessage(ctx, value, mv)) return err;
    if (t_env.outbox == nullptr) return ctx->createUndefined();

    Message msg;
    msg.value = std::move(mv);
    t_env.outbox->push(std::move(msg));

    // 唤醒主线程事件循环：外部任务在 owner 线程派发 onmessage。
    if (t_env.mainCtx != nullptr && t_env.state) {
        auto state = t_env.state;
        t_env.mainCtx->postExternalTask(
            [state](IContext* c) { dispatchToMain(c, state.get()); });
    }
    return ctx->createUndefined();
}

// worker 线程：执行入口脚本（classic script）。
void runWorkerScript(IContext* ctx, const std::string& source,
                     const std::string& path) {
    frontend::Parser parser(source,
                            frontend::ParserOptions{frontend::SourceType::Script});
    frontend::ParseResult parsed = parser.parseProgram();
    if (!parsed.ok()) {
        reportWorkerError(ctx, "failed to parse " + path);
        return;
    }

    sema::SemaResult sema = sema::runSema(*parsed.program);
    if (!sema.ok()) {
        reportWorkerError(ctx, "failed to analyze " + path);
        return;
    }

    compiler::Compiler compiler(
        static_cast<const frontend::Program&>(*parsed.program), sema,
        compiler::CompilerOptions{path});
    compiler::CompileResult compiled = compiler.compile();
    if (!compiled.ok()) {
        reportWorkerError(ctx, "failed to compile " + path);
        return;
    }

    // 脚本创建的函数可能逃逸并长期引用 Code，故交给 Context 持有。
    vm::Code* code = compiled.code.get();
    ctx->retainCode(std::move(compiled.code));
    vm::VM machine(ctx, code);
    Value* result = machine.run();
    if (result != nullptr && result->type()->kind() == TypeKind::Exception) {
        reportWorkerError(ctx, "uncaught exception in " + path);
    }
}

// worker 线程 setup：注册 worker 全局并运行入口脚本。
void jsWorkerSetup(IContext* ctx, MessageQueue& outbox, void* userdata) {
    auto* statePtr = static_cast<std::shared_ptr<WorkerState>*>(userdata);
    t_env.outbox = &outbox;
    t_env.mainCtx = (*statePtr)->mainCtx;
    t_env.state = *statePtr;

    Value* post = ctx->createNativeFunction(&workerPostMessageGlobal, 1);
    if (post != ctx->oomValue()) {
        ctx->setFunctionName(post, "postMessage");
        ctx->defineGlobal("postMessage", post, true, false, true);
    }
    // onmessage 初始为 null；脚本通过赋值覆盖。
    ctx->defineGlobal("onmessage", ctx->createNull(), true, false, true);

    runWorkerScript(ctx, (*statePtr)->source, (*statePtr)->path);
}

// worker 线程：每条 inbox 消息调用一次 onmessage({data})。
void jsWorkerHandler(IContext* ctx, const Message& in, MessageQueue& /*outbox*/,
                     void* /*userdata*/) {
    Value* handler = ctx->getProperty(ctx->globalObject(), "onmessage");
    if (!detail::isCallable(handler)) return;

    Value* data = worker::messageToValue(ctx, in.value);
    if (detail::isErrorValue(data)) return;

    Value* event = ctx->createObject();
    if (event == ctx->oomValue()) return;
    ctx->defineProperty(event, "data", data);
    (void)ctx->call(handler, ctx->createUndefined(), 1, &event);
}

Value* workerCtorCall(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                      Value* /*argv*/[]) {
    return detail::typeError(ctx, "Worker constructor requires 'new'");
}

Value* workerConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                       Value* argv[]) {
    if (argc < 1) {
        return detail::typeError(ctx, "Worker requires a script path");
    }
    Value* pathValue = ctx->toString(argv[0]);
    if (detail::isErrorValue(pathValue)) return pathValue;
    std::string path = static_cast<StringData*>(pathValue->data())->value;

    // 主线程读取脚本源码；worker 线程只负责编译执行。
    auto fs = infra::createNativeFileSystem();
    infra::Result<std::string> source = fs->readFile(path);
    if (source.isErr()) {
        return ctx->createErrorException(
            ErrorKind::Error, "Failed to load worker script: " + path);
    }

    auto* data = ctx->gc()->allocData<WorkerData>();
    if (data == nullptr) return ctx->oomValue();
    data->state = std::make_shared<WorkerState>();
    data->state->mainCtx = ctx;
    data->state->path = path;
    data->state->source = std::move(source.value());

    WorkerCallbacks callbacks;
    callbacks.setup = &jsWorkerSetup;
    callbacks.handler = &jsWorkerHandler;
    callbacks.userdata = &data->state;
    data->worker = worker::createWorker(ctx->allocator(), ctx->console(),
                                        callbacks, WorkerOptions{});
    if (!data->worker) {
        return ctx->createErrorException(ErrorKind::Error,
                                         "Failed to create worker");
    }
    data->state->worker = data->worker.get();

    Value* object = ctx->createWorkerObject(data);
    if (object == ctx->oomValue()) return object;
    data->state->workerObject = object;

    // onmessage 是普通自有属性（事件处理器属性），初始 null。
    ctx->defineProperty(object, "onmessage", ctx->createNull(), true, false,
                        true);

    if (!data->worker->start()) {
        return ctx->createErrorException(ErrorKind::Error,
                                         "Failed to start worker");
    }
    return object;
}

Value* workerPostMessageMethod(IContext* ctx, Value* self, size_t argc,
                               Value* argv[]) {
    WorkerData* data = asWorker(self);
    if (data == nullptr) {
        return detail::typeError(
            ctx, "Worker.prototype.postMessage called on incompatible receiver");
    }
    if (!data->state || !data->state->alive.load(std::memory_order_acquire)) {
        return ctx->createUndefined();
    }

    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    MessageValue mv;
    if (Value* err = worker::valueToMessage(ctx, value, mv)) return err;

    Message msg;
    msg.value = std::move(mv);
    data->worker->post(std::move(msg));
    return ctx->createUndefined();
}

Value* workerTerminateMethod(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    WorkerData* data = asWorker(self);
    if (data == nullptr) {
        return detail::typeError(
            ctx, "Worker.prototype.terminate called on incompatible receiver");
    }
    if (data->state) data->state->alive.store(false, std::memory_order_release);
    if (data->worker) {
        data->worker->requestStop();
        data->worker->join();
    }
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

bool registerWorkerBuiltins(IContext* ctx) {
    Value* proto = ctx->createObject();
    if (proto == ctx->oomValue()) return false;

    Value* ctor = ctx->createNativeFunction(&workerCtorCall, 1);
    if (ctor == ctx->oomValue()) return false;
    ctx->setFunctionName(ctor, "Worker");
    ctx->setFunctionConstructor(ctor, &workerConstruct);

    if (ctx->defineProperty(ctor, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(proto, "constructor", ctor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, proto, "postMessage", &workerPostMessageMethod, 1)) {
        return false;
    }
    if (!defineMethod(ctx, proto, "terminate", &workerTerminateMethod, 0)) {
        return false;
    }

    ctx->setWorkerPrototype(proto);
    ctx->defineGlobal("Worker", ctor, true, false, true);
    return true;
}

}  // namespace neo::core
