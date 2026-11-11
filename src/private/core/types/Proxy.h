// types/Proxy.h - Proxy exotic 对象（ES2024 §10.5 / §28.2）
//
// Proxy 不是普通对象：它没有自己的属性/原型，所有内部方法都转发到
// [[ProxyTarget]] 与 [[ProxyHandler]] 上的 trap。因此：
//   - ProxyData 仍派生 ObjectData（复用对象布局判定/GC/私有槽），
//     但 properties/order/symbols/prototype 不使用；
//   - ProxyType 覆盖全部属性/原型/调用/构造内部方法；
//   - 可调用/可构造性继承自 target（由 TypeBehavior::isCallable/isConstructor 判定）。
//
// 撤销（Proxy.revocable）：[[ProxyHandler]] 置 nullptr，此后任何内部方法
// 都抛 TypeError（ES2024 §10.5.1）。
//
// 关联 ADR: 018（对象模型）、048（Proxy 内部方法层）

#ifndef NEO_CORE_PRIVATE_TYPES_PROXY_H
#define NEO_CORE_PRIVATE_TYPES_PROXY_H

#include "core/types/Object.h"

namespace neo::core {

class ProxyData : public ObjectData {
public:
    Value* target = nullptr;   // [[ProxyTarget]]
    Value* handler = nullptr;  // [[ProxyHandler]]；nullptr = 已撤销

    // GC 遍历：target + handler（其余对象字段不使用，仍调用基类保持一致）。
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this。
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

class ProxyType : public ObjectType {
public:
    ProxyType() : ObjectType(TypeKind::Proxy) {}
    const char* name() const override { return "proxy"; }

    // 运算/ToString 继承 ObjectType（经 ToPrimitive → getProperty 触发 trap）。

    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;

    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;

    // 内部方法层（逐跳分派入口）。
    Value* getOwnProperty(IContext* ctx, Value* self, const PropertyKey& key,
                          bool& exists, Property& out) const override;
    Value* ownKeys(IContext* ctx, Value* self,
                   std::vector<Value*>& out) const override;
    Value* getWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                           Value* receiver) override;
    Value* setWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                           Value* value, Value* receiver, bool& ok) override;
};

// 取 Proxy 的 [[ProxyTarget]]；非 Proxy 返回 nullptr。
Value* proxyTarget(Value* v);
// 取 Proxy 的 [[ProxyHandler]]；非 Proxy 或已撤销返回 nullptr。
Value* proxyHandler(Value* v);
// 撤销 Proxy（Proxy.revocable 的 revoke 调用）。
void revokeProxy(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_PROXY_H
