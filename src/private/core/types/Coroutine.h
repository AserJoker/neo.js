// types/Coroutine.h - CoroutineType + CoroutineData
//
// 协程对象（ADR-045）：可恢复的计算句柄。命名属性/符号属性/原型链复用对象模型
// （Data 继承 ObjectData，与 Promise/Iterator 一致），内部槽用专用字段：
//
//   - state          CoroutineState       Suspended / Running / Completed
//   - resumePoint    断点（native=stage，VM=PC；抽象层视为不透明整数）
//   - yielded        最近一次 yield 的值；Completed 后为最终返回值
//   - sentValue      本次恢复传入的值（普通值，或 Interrupt 的 throw/return 注入）
//   - target         被执行的函数对象
//   - receiver       执行时的 this
//   - initialArgs    首次运行的实参
//   - execScope      保存的执行作用域子树根（挂起期间孤立；首次运行前为 nullptr）
//   - savedCallStack 挂起时的调用栈快照（ADR-037）
//   - resume         恢复驱动（多态入口，ADR-045 §4）
//   - vmState        未来 VM 的挂起状态（native 恒为 nullptr）
//
// GC：execScope 是孤立子树，不在 global 树上，只能由本 data 在标记时主动
// visitScope 递归（与 FunctionData::closureScope 同一机制，ADR-044）。
// 协程不可达 → 其作用域树与捕获值都不被标记，环整体可回收。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、036（闭包作用域孤立化）、
//          037（调用栈）、044（可达性标记）、045（协程抽象）

#ifndef NEO_CORE_PRIVATE_TYPES_COROUTINE_H
#define NEO_CORE_PRIVATE_TYPES_COROUTINE_H

#include "core/CallStack.h"
#include "core/Coroutine.h"
#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"  // CoroutineData 继承 ObjectData
#include "infra/IAllocator.h"

#include <cstdint>
#include <vector>

namespace neo::core {

class Scope;
class Value;

class CoroutineType : public IType {
public:
    // 协程具备对象布局（Data 派生自 ObjectData），故 objectLayout=true。
    CoroutineType() : IType(TypeKind::Coroutine, true) {}
    // 供 AsyncGeneratorType 复用同一套对象行为，仅换 TypeKind。
    explicit CoroutineType(TypeKind k) : IType(k, true) {}
    const char* name() const override { return "coroutine"; }

    // 运算：走 ToPrimitive（与普通对象一致）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

    // 协程实例不是函数也不是构造函数
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

// 协程负载：继承 ObjectData 复用属性容器/原型链/GC。
class CoroutineData : public ObjectData {
public:
    CoroutineState state = CoroutineState::Suspended;  // 状态机
    uint64_t resumePoint = 0;                          // 断点（native stage / VM PC）
    Value* yielded = nullptr;                          // 最近 yield 值 / 最终返回值
    Value* sentValue = nullptr;                        // 本次恢复传入的值
    Value* target = nullptr;                           // 被执行的函数对象
    Value* receiver = nullptr;                         // 执行时的 this
    std::vector<Value*> initialArgs;                   // 首次运行的实参
    Scope* execScope = nullptr;                        // 孤立执行作用域子树根
    std::vector<CallFrame> savedCallStack;             // 挂起时的调用栈快照
    CoroutineRunner resume = nullptr;                  // 恢复驱动（多态）
    Value* vmState = nullptr;                          // 未来 VM 挂起状态

    ~CoroutineData() override;

    // GC 遍历：对象字段 + 内部槽持有值 + visitScope(execScope)
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

// 内部槽访问：非 Coroutine 返回 nullptr。
CoroutineData* asCoroutine(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_COROUTINE_H
