// types/Promise.h - PromiseType + PromiseData
//
// Promise 对象（ES2024 §27.2）：普通对象 + Promise 内部槽。
// 命名属性/符号属性/原型链全部复用 ObjectData（与 ArrayData/FunctionData 一致）。
//
// 内部槽（§27.2.1，用专用字段表达，不放进属性容器）：
//   - state       [[PromiseState]]       Pending / Fulfilled / Rejected
//   - result      [[PromiseResult]]      完成值或拒绝原因（Pending 时为 undefined）
//   - reactions   [[PromiseReactions]]   Pending 期间注册的 reaction 列表
//   - isHandled   [[PromiseIsHandled]]   是否已被处理（未处理拒绝上报用，ADR-043）
//
// 一个 reaction（§27.2.1.1 PromiseReaction Record）：
//   - capability   由 then 创建的派生 promise 的 capability（onFulfilled/onRejected/
//                  promise 三元组）；nullptr 表示「仅用于未处理拒绝跟踪」的 reaction
//   - type         Fulfill / Reject（handler 缺失或缺省时的透传方向）
//   - handler      用户提供的 onFulfilled / onRejected（可为空 = 透传）
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、019（赋值语义 §2）、043（Promise）

#ifndef NEO_CORE_PRIVATE_TYPES_PROMISE_H
#define NEO_CORE_PRIVATE_TYPES_PROMISE_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"  // PromiseData 继承 ObjectData
#include "infra/IAllocator.h"

#include <vector>

namespace neo::core {

class Value;

// [[PromiseState]]（ES2024 §27.2.1.1 表 84）
enum class PromiseState : uint8_t {
    Pending,
    Fulfilled,
    Rejected,
};

// PromiseReaction 的 type 字段（§27.2.1.1 表 85）
enum class PromiseReactionType : uint8_t {
    Fulfill,
    Reject,
};

// PromiseCapability Record（§27.2.1.1）：{ promise, resolve, reject }
struct PromiseCapability {
    Value* promise = nullptr;
    Value* resolve = nullptr;
    Value* reject = nullptr;
};

// PromiseReaction Record（§27.2.1.1）。
// capability 为空表示「只用于未处理拒绝跟踪」的 reaction（§27.2.6.1 step 9）。
struct PromiseReaction {
    PromiseCapability capability;
    PromiseReactionType type = PromiseReactionType::Fulfill;
    Value* handler = nullptr;  // 用户 handler；nullptr = 透传
};

class PromiseType : public IType {
public:
    // Promise 具备对象布局（Data 派生自 ObjectData），故 objectLayout=true。
    PromiseType() : IType(TypeKind::Promise, true) {}
    const char* name() const override { return "promise"; }

    // 运算：走 ToPrimitive（与普通对象一致）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

    // Promise 实例不是函数也不是构造函数
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;

    // 属性/原型操作全部委托 ObjectType
    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key, Value* value,
                          bool writable, bool enumerable, bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// Promise 负载：继承 InstanceData 复用属性容器/原型链/GC 与私有槽。
class PromiseData : public InstanceData {
public:
    PromiseState state = PromiseState::Pending;  // [[PromiseState]]
    Value* result = nullptr;                     // [[PromiseResult]]（Pending 时 undefined）
    std::vector<PromiseReaction> reactions;      // [[PromiseReactions]]
    bool isHandled = false;                      // [[PromiseIsHandled]]
    // 本 promise 的 resolving functions（resolve/reject）。
    // 它们在构造期的临时 execScope 中创建，execScope 销毁后不再从该作用域可达；
    // 由本 promise 强持有（enumerateChildren 登记）保证存活到被调用。
    // 反向引用走 boundArgs（data 级引用，非 GC 根），避免
    // 「promise → 函数 → 闭包作用域(根) → promise」的不可回收根环。
    Value* resolveFn = nullptr;
    Value* rejectFn = nullptr;

    // GC 遍历：对象字段 + result + 每个 reaction 的 capability 与 handler
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

// 内部槽访问：非 Promise 返回 nullptr。
PromiseData* asPromise(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_PROMISE_H
