// EventTarget.h - Worker/MessagePort 共用的最小事件监听器支持
//
// 只实现 "message" 事件：addEventListener/removeEventListener 把回调保存在
// 目标对象的一个非枚举自有属性（数组）中；派发时依次调用。
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_EVENTTARGET_H
#define NEO_CORE_PRIVATE_EVENTTARGET_H

namespace neo::core {

class IContext;
class Value;

// 在原型上安装 addEventListener / removeEventListener。
bool installEventTargetMethods(IContext* ctx, Value* proto);

// 初始化目标对象的监听器存储（创建时调用）。
void initEventListenerStore(IContext* ctx, Value* target);

// 把 event 派发给目标对象上注册的 "message" 监听器。
void dispatchEventListeners(IContext* ctx, Value* target, Value* event);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_EVENTTARGET_H
