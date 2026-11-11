// types/primitive_wrapper.h - PrimitiveWrapperType + PrimitiveWrapperData
//
// 原始值包装对象（ES2024 §10.4）：Boolean / Number / String / Symbol 的 new 形式。
// 四种包装对象结构完全一致，共用同一个 Type 与 Data 类，差异只在
// [[PrimitiveValue]] 的类型与各自的 prototype 对象。
//
// 内部槽 [[PrimitiveValue]] 用专用字段 primitiveValue 表达（不放进 ObjectData 的
// 属性容器）：零查找成本、类型安全、普通对象零额外开销。
//
// 对象行为（属性/原型/访问器/GC）全部委托 ObjectType（与 FunctionData/ArrayData 一致）。
// 例外：String 包装对象的 length 与索引属性（ES2024 §10.4.3.1/§10.4.3.2）由本类实现。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、019（赋值语义 §2）

#ifndef NEO_CORE_PRIVATE_TYPES_PRIMITIVE_WRAPPER_H
#define NEO_CORE_PRIVATE_TYPES_PRIMITIVE_WRAPPER_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"  // PrimitiveWrapperData 继承 ObjectData
#include "infra/IAllocator.h"

#include <string>

namespace neo::core {

class Value;

class PrimitiveWrapperType : public IType {
public:
    PrimitiveWrapperType() : IType(TypeKind::PrimitiveWrapper, true) {}
    const char* name() const override { return "object"; }

    // String 包装具备索引元素与 length 自有属性（Boolean/Number/Symbol 包装没有）。
    bool hasIndexedElements() const override;
    bool hasIndexedLength(Value* self) const override;
    uint32_t indexedLength(Value* self) const override;
    Value* indexedElement(IContext* ctx, Value* self, uint32_t index) const override;

    // 运算：包装对象需 ToPrimitive（拆 [[PrimitiveValue]] 后按原始值规则）。
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

    // 包装对象不是函数也不是构造函数（Boolean/Number/... 构造函数是 Function 类型）
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;

    // 属性操作：String 包装的 length/索引本地处理，其余委托 ObjectType
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

// 包装对象负载：继承 ObjectData 复用属性容器/原型链/GC 与私有槽，
// 增加 [[PrimitiveValue]]（class extends Boolean/Number/String）。
class PrimitiveWrapperData : public ObjectData {
public:
    // [[PrimitiveValue]]：被包装的原始值（Boolean/Number/String/Symbol 之一）。
    // 由构造方保证非空且类型合法。
    Value* primitiveValue = nullptr;

    // GC 遍历：先报告继承的对象字段，再报告被包装的原始值
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

// 取包装对象的 [[PrimitiveValue]]；非包装对象返回 nullptr。
Value* wrapperPrimitiveValue(Value* v);

// 取包装对象的原始值字符串（String 包装返回被包装字符串，其他返回空串）。
const std::string* wrapperStringValue(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_PRIMITIVE_WRAPPER_H
