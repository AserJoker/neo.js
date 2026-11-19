// EventTarget.cpp - Worker/MessagePort 的最小事件监听器实现

#include "core/EventTarget.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// 监听器数组存放在目标对象的非枚举自有属性中；ObjectData 的 GC 遍历会覆盖它。
constexpr const char* kListenersKey = "__neo_listeners__";

Value* listenersOf(IContext* ctx, Value* target) {
    Value* list = ctx->getProperty(target, kListenersKey);
    if (list->type()->kind() == TypeKind::Array) return list;
    Value* created = ctx->createArray();
    if (created == ctx->oomValue()) return created;
    Value* defined =
        ctx->defineProperty(target, kListenersKey, created, true, false, true);
    if (detail::isErrorValue(defined)) return defined;
    return created;
}

Value* addEventListenerMethod(IContext* ctx, Value* self, size_t argc,
                              Value* argv[]) {
    if (argc < 2) return ctx->createUndefined();
    Value* typeValue = ctx->toString(argv[0]);
    if (detail::isErrorValue(typeValue)) return typeValue;
    std::string type = static_cast<StringData*>(typeValue->data())->value;
    if (type != "message") return ctx->createUndefined();
    if (!detail::isCallable(argv[1])) return ctx->createUndefined();

    Value* list = listenersOf(ctx, self);
    if (detail::isErrorValue(list)) return list;
    auto* data = static_cast<ArrayData*>(list->data());
    for (uint32_t i = 0; i < data->length(); ++i) {
        Value* existing = data->elementAt(i);
        if (existing != nullptr && detail::strictEquals(existing, argv[1])) {
            return ctx->createUndefined();  // 同一回调不重复注册
        }
    }
    return ctx->arrayAppend(list, argv[1]);
}

Value* removeEventListenerMethod(IContext* ctx, Value* self, size_t argc,
                                 Value* argv[]) {
    if (argc < 2) return ctx->createUndefined();
    Value* typeValue = ctx->toString(argv[0]);
    if (detail::isErrorValue(typeValue)) return typeValue;
    std::string type = static_cast<StringData*>(typeValue->data())->value;
    if (type != "message") return ctx->createUndefined();

    Value* list = ctx->getProperty(self, kListenersKey);
    if (list->type()->kind() != TypeKind::Array) return ctx->createUndefined();
    auto* data = static_cast<ArrayData*>(list->data());
    for (uint32_t i = 0; i < data->length(); ++i) {
        Value* existing = data->elementAt(i);
        if (existing != nullptr && detail::strictEquals(existing, argv[1])) {
            data->removeElement(i);
            break;
        }
    }
    return ctx->createUndefined();
}

}  // namespace

bool installEventTargetMethods(IContext* ctx, Value* proto) {
    auto define = [ctx, proto](const char* name, NativeFn fn) {
        Value* f = ctx->createNativeFunction(fn, 2);
        if (f == ctx->oomValue()) return false;
        ctx->setFunctionName(f, name);
        return ctx->defineProperty(proto, name, f, true, true, true)
                   ->type()->kind() != TypeKind::Exception;
    };
    return define("addEventListener", &addEventListenerMethod) &&
           define("removeEventListener", &removeEventListenerMethod);
}

void initEventListenerStore(IContext* ctx, Value* target) {
    Value* list = ctx->createArray();
    if (list == ctx->oomValue()) return;
    (void)ctx->defineProperty(target, kListenersKey, list, true, false, true);
}

void dispatchEventListeners(IContext* ctx, Value* target, Value* event) {
    Value* list = ctx->getProperty(target, kListenersKey);
    if (list->type()->kind() != TypeKind::Array) return;
    auto* data = static_cast<ArrayData*>(list->data());
    for (uint32_t i = 0; i < data->length(); ++i) {
        Value* handler = data->elementAt(i);
        if (handler == nullptr || !detail::isCallable(handler)) continue;
        (void)ctx->call(handler, target, 1, &event);
    }
}

}  // namespace neo::core
