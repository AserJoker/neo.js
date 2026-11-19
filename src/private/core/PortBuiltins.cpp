// PortBuiltins.cpp - MessagePort / MessageChannel
//
// 一对端口共享 PortChannel；postMessage 把消息放入对端队列并唤醒对端 owner
// 的 Context（外部任务）。onmessage 作为宏任务派发。
//
// 转移：postMessage(value, [port]) 把端点交给接收方；源对象 detached=true，
// 接收方通过 MessageValue 还原创建新的端口对象并安装自己的 wake。

#include "core/PortBuiltins.h"

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/MessagePort.h"
#include "core/types/TypeBehavior.h"
#include "worker/MessageClone.h"
#include "worker/Port.h"

#include <memory>
#include <utility>
#include <vector>

namespace neo::core {

namespace {

using worker::Message;
using worker::MessagePortEndpoint;
using worker::PortWake;

void dispatchPort(IContext* ctx,
                  const std::shared_ptr<MessagePortEndpoint>& endpoint) {
    if (!endpoint || endpoint->closed()) return;
    if (endpoint->ownerCtx() != ctx) return;  // 已被转移，旧 owner 不再派发
    Value* portObject = endpoint->portObject();
    if (portObject == nullptr) return;

    Message msg;
    while (endpoint->channel()->queues[endpoint->side()].tryPop(msg)) {
        Value* handler = ctx->getProperty(portObject, "onmessage");
        if (!detail::isCallable(handler)) continue;

        Value* data = worker::messageToValue(ctx, msg.value);
        if (detail::isErrorValue(data)) continue;

        Value* event = ctx->createObject();
        if (event == ctx->oomValue()) return;
        ctx->defineProperty(event, "data", data);
        (void)ctx->call(handler, portObject, 1, &event);
    }
}

Value* createPortObject(IContext* ctx,
                        std::shared_ptr<MessagePortEndpoint> endpoint) {
    auto* data = ctx->gc()->allocData<MessagePortData>();
    if (data == nullptr) return ctx->oomValue();
    data->endpoint = endpoint;

    Value* object = ctx->createMessagePortObject(data);
    if (object == ctx->oomValue()) return object;

    // onmessage 先就绪，再安装 wake，避免派发时属性尚未定义。
    ctx->defineProperty(object, "onmessage", ctx->createNull(), true, false,
                        true);
    endpoint->attach(ctx, object, &dispatchPort);
    data->wake = endpoint->wakeSnapshot();

    // 安装 wake 前可能已有消息入队：补一次唤醒。
    if (!endpoint->channel()->queues[endpoint->side()].empty()) {
        auto wake = endpoint->wakeSnapshot();
        if (wake && wake->alive.load(std::memory_order_acquire)) {
            wake->ctx->postExternalTask(
                [endpoint](IContext* c) { dispatchPort(c, endpoint); });
        }
    }
    return object;
}

Value* portPostMessageMethod(IContext* ctx, Value* self, size_t argc,
                             Value* argv[]) {
    MessagePortData* data = asMessagePort(self);
    if (data == nullptr || data->detached) {
        return detail::typeError(
            ctx, "MessagePort.prototype.postMessage called on detached port");
    }

    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    std::vector<Value*> transfer;
    if (Value* err = worker::resolveTransferList(
            ctx, argc > 1 ? argv[1] : nullptr, transfer)) {
        return err;
    }
    worker::MessageValue mv;
    if (Value* err = worker::valueToMessage(ctx, value, mv, transfer)) return err;
    worker::applyTransfer(transfer);

    Message msg;
    msg.value = std::move(mv);
    data->endpoint->post(std::move(msg));
    return ctx->createUndefined();
}

Value* portStartMethod(IContext* ctx, Value* self, size_t /*argc*/,
                       Value* /*argv*/[]) {
    if (asMessagePort(self) == nullptr) {
        return detail::typeError(ctx,
                                 "MessagePort.prototype.start called on "
                                 "incompatible receiver");
    }
    // onmessage 已自动启动；start 仅作兼容入口。
    return ctx->createUndefined();
}

Value* portCloseMethod(IContext* ctx, Value* self, size_t /*argc*/,
                       Value* /*argv*/[]) {
    MessagePortData* data = asMessagePort(self);
    if (data == nullptr) {
        return detail::typeError(ctx,
                                 "MessagePort.prototype.close called on "
                                 "incompatible receiver");
    }
    if (!data->detached && data->endpoint) data->endpoint->close();
    return ctx->createUndefined();
}

Value* messagePortCtor(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                       Value* /*argv*/[]) {
    return detail::typeError(ctx, "Illegal constructor");
}

Value* messageChannelConstruct(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                               Value* /*argv*/[]) {
    auto [p0, p1] = MessagePortEndpoint::createPair();
    Value* port1 = createPortObject(ctx, std::move(p0));
    if (detail::isErrorValue(port1)) return port1;
    Value* port2 = createPortObject(ctx, std::move(p1));
    if (detail::isErrorValue(port2)) return port2;

    Value* channel = ctx->createObject();
    if (channel == ctx->oomValue()) return channel;
    ctx->defineProperty(channel, "port1", port1, true, true, true);
    ctx->defineProperty(channel, "port2", port2, true, true, true);
    return channel;
}

Value* messageChannelCtorCall(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                              Value* /*argv*/[]) {
    return detail::typeError(ctx, "MessageChannel constructor requires 'new'");
}

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, true, true)->type()->kind() !=
           TypeKind::Exception;
}

bool registerCtor(IContext* ctx, const char* name, NativeFn call, NativeFn ctor,
                  Value* proto) {
    Value* fn = ctx->createNativeFunction(call, 0);
    if (fn == ctx->oomValue()) return false;
    ctx->setFunctionName(fn, name);
    ctx->setFunctionConstructor(fn, ctor);
    if (ctx->defineProperty(fn, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(proto, "constructor", fn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    ctx->defineGlobal(name, fn, true, false, true);
    return true;
}

}  // namespace

Value* createTransferredPort(
    IContext* ctx, std::shared_ptr<worker::MessagePortEndpoint> endpoint) {
    return createPortObject(ctx, std::move(endpoint));
}

bool registerMessagePortBuiltins(IContext* ctx) {
    Value* portProto = ctx->createObject();
    Value* channelProto = ctx->createObject();
    if (portProto == ctx->oomValue() || channelProto == ctx->oomValue()) {
        return false;
    }

    if (!defineMethod(ctx, portProto, "postMessage", &portPostMessageMethod, 1)) {
        return false;
    }
    if (!defineMethod(ctx, portProto, "start", &portStartMethod, 0)) return false;
    if (!defineMethod(ctx, portProto, "close", &portCloseMethod, 0)) return false;

    ctx->setMessagePortPrototype(portProto);

    if (!registerCtor(ctx, "MessagePort", &messagePortCtor, &messagePortCtor,
                      portProto)) {
        return false;
    }
    if (!registerCtor(ctx, "MessageChannel", &messageChannelCtorCall,
                      &messageChannelConstruct, channelProto)) {
        return false;
    }
    return true;
}

}  // namespace neo::core
