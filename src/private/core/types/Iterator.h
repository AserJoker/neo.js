// types/Iterator.h - ArrayIteratorType/ArrayIteratorData + IteratorWrapperType/IteratorWrapperData
//
// 迭代器对象（ES2024 §23.1.5 Array Iterator Objects / ES2025 §27.1.4.2 Iterator.from）：
// 两者都在对象模型基础上增加内部槽，命名属性/符号属性/原型链复用 ObjectData
// （与 ArrayData/FunctionData 一致，Data 继承 ObjectData）。
//
// ArrayIteratorData 内部槽（ES2024 §23.1.5.1 CreateArrayIterator）：
//   - iteratedObject   [[IteratedObject]]       被迭代的数组（null 表示已耗尽）
//   - nextIndex        [[ArrayIteratorNextIndex]] 下一个索引
//   - kind             [[ArrayIterationKind]]   key / value / key+value
// 迭代是 live 的：每次 next 重新读取数组的 length 与元素（ES2024 §23.1.5.2.1）。
//
// IteratorWrapperData 内部槽（ES2025 §27.1.4.2）：
//   - iterated         [[Iterated]] 被包装的 iterator-like 对象
//   - nextMethod       [[Iterated]] 的 next 方法（调用时以 iterated 为 this）
// 仅 Iterator.from 在输入对象不是"真迭代器"时创建。
//
// 两个 Type 的行为完全一致（属性/原型委托 ObjectType，运算走 ToPrimitive，
// call/construct 报错），故共用 IteratorObjectType 基类。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、019（赋值语义 §2）

#ifndef NEO_CORE_PRIVATE_TYPES_ITERATOR_H
#define NEO_CORE_PRIVATE_TYPES_ITERATOR_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/IteratorKind.h"
#include "core/types/Object.h"  // Data 继承 ObjectData
#include "infra/IAllocator.h"

#include <cstdint>
#include <new>

namespace neo::core {

class Value;

// 迭代器对象的共同行为：无特化属性语义，全部委托 ObjectType。
class IteratorObjectType : public IType {
public:
    // 迭代器具备对象布局（Data 派生自 ObjectData），故 objectLayout=true。
    explicit IteratorObjectType(TypeKind k) : IType(k, true) {}

    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

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
};

class ArrayIteratorType : public IteratorObjectType {
public:
    ArrayIteratorType() : IteratorObjectType(TypeKind::ArrayIterator) {}
    const char* name() const override { return "array iterator"; }
};

// 数组迭代器负载：继承 ObjectData 复用 properties/order/symbols/prototype 与 GC 遍历。
class ArrayIteratorData : public ObjectData {
public:
    Value* iteratedObject = nullptr;  // [[IteratedObject]]（nullptr = 已耗尽）
    uint32_t nextIndex = 0;           // [[ArrayIteratorNextIndex]]
    ArrayIterationKind kind = ArrayIterationKind::Value;  // [[ArrayIterationKind]]

    // GC 遍历：对象字段 + 被迭代数组（否则迭代期间数组可能被回收）
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

class IteratorWrapperType : public IteratorObjectType {
public:
    IteratorWrapperType() : IteratorObjectType(TypeKind::IteratorWrapper) {}
    const char* name() const override { return "iterator wrapper"; }
};

// 迭代器包装负载：继承 ObjectData 复用对象模型。
class IteratorWrapperData : public ObjectData {
public:
    Value* iterated = nullptr;    // [[Iterated]]（被包装的 iterator-like 对象）
    Value* nextMethod = nullptr;  // [[Iterated]] 的 next 方法

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

// 内部槽访问（非对应类型返回 nullptr）
Value* arrayIteratorIteratedObject(Value* v);
IteratorWrapperData* asIteratorWrapper(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_ITERATOR_H
