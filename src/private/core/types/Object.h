// types/object.h - ObjectType + ObjectData + Property
//
// 普通对象（ADR-018 对象模型）：
//   properties —— hashmap（string key → Property，主存储）
//   order      —— vector<string>（只存 key 拷贝，保序遍历）
//   symbols    —— hashmap（SymbolData* → SymbolProperty，符号键分离）
//   prototype  —— 原型链指针（Value*，可空）
//
// 属性键（ToPropertyKey，ES2024 §7.1.19）：属性 API 的 key 是 Value*。
//   - symbol 保持符号身份，走 symbols 容器（按 SymbolData* 身份查找，描述不参与）；
//   - 其他原始类型 ToString 后走 properties 容器（1 和 "1" 是同一个键）。
//
// Property 是属性描述符（ADR-018 §2）：value + writable/enumerable/configurable + getter/setter。
// 访问器属性（getter/setter）已启用，见 ObjectType::getProperty/setProperty。
//
// GC 遍历（ADR-018 §5）：enumerateChildren 报告 prototype + 所有 property 的
// value/getter/setter，以及 symbol 属性持有的 symbol key Value（保活键）。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、016（Type 体系）

#ifndef NEO_CORE_PRIVATE_TYPES_OBJECT_H
#define NEO_CORE_PRIVATE_TYPES_OBJECT_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/PrivateName.h"
#include "infra/IAllocator.h"

#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace neo::core {

class Value;

// 是否访问器属性（getter/setter 任一非空）
inline bool isAccessorDescriptor(const Property& p) {
    return p.getter != nullptr || p.setter != nullptr;
}

// symbol 属性描述符：额外持有 symbol key 本身（GC 需保活 key，且删除时需按身份比对）
struct SymbolProperty {
    Value* key = nullptr;     // symbol key（Value*，类型必为 Symbol）
    Property prop;
};

class ObjectType : public IType {
public:
    ObjectType() : IType(TypeKind::Object, true) {}
    const char* name() const override { return "object"; }

protected:
    // 派生 exotic 对象（如 ModuleNamespace）复用对象布局，但使用自己的 TypeKind。
    explicit ObjectType(TypeKind kind) : IType(kind, true) {}

public:
    // 运算：本阶段返回 Exception("not implemented")（对象参与运算需 ToPrimitive，阶段 5+）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

    // 调用：非函数，返回 Exception
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;

    // 属性操作（ADR-018）
    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    Value* hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 私有槽（ADR-047 §5）：name 是 class 求值唯一的 PrivateName；
// value == nullptr 表示只有 brand（私有方法/访问器），非空表示私有字段值。
struct PrivateSlot {
    PrivateName* name = nullptr;
    Value* value = nullptr;
};

using PrivateSlots = std::vector<PrivateSlot>;

class ObjectData : public IData {
public:
    std::unordered_map<std::string, Property> properties;  // 字符串键主存储
    std::vector<std::string> order;                        // 字符串键保序遍历（key 拷贝）
    // symbol 键：按 SymbolData* 身份哈希（描述不参与），值携带 key Value 供 GC 保活
    std::unordered_map<const void*, SymbolProperty> symbols;
    std::vector<const void*> symbolOrder;                  // symbol 键保序遍历
    Value* prototype = nullptr;                            // 原型链指针（可空）
    bool extensible = true;                                // [[Extensible]]（ES2024 §10.1.3）
    // 私有槽（ADR-047 §5）：所有对象都可承载，保证 super() 返回任意对象时
    // 派生类私有字段仍可初始化。空 vector 不分配堆；后续用轻量容器优化。
    PrivateSlots slots;

    // 析构：归还槽位持有的 PrivateName 引用（引用计数）。
    ~ObjectData() override;

    // GC 遍历：报告 prototype + 所有 property 的 value/getter/setter
    // + symbol 属性的 key/setter/getter/value + 私有字段值
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this，不复制容器
    IData* clone(neo::infra::IAllocator* alloc) const override;

    // 私有槽访问（ADR-047 §5）。
    PrivateSlots* privateSlots() { return &slots; }
};

// 枚举自有属性键（ES2024 §10.1.11 OrdinaryOwnPropertyKeys 顺序）：
//   1. 数组索引（升序，仅 Array 与 String 包装有）
//   2. 字符串键（插入序；Array/String 包装的 "length" 也在此列）
//   3. symbol 键（插入序）
//
// enumerableOnly：只含 enumerable 的键（Object.keys 语义）；
// includeSymbols：是否包含 symbol 键（Reflect.ownKeys 语义）。
// 非对象（原始值）返回空 vector。成功返回 nullptr 并写入 out；
// Proxy trap 抛错返回 Exception/Fatal。返回的 Value* 均已归属 current scope。
Value* ownPropertyKeys(IContext* ctx, Value* obj, bool enumerableOnly,
                       bool includeSymbols, std::vector<Value*>& out);

// ToPropertyKey（ES2024 §7.1.19）：把任意 Value* 规范化为 PropertyKey。
// 成功返回 nullptr；失败返回 Exception/Fatal（对象键的 ToPrimitive 抛错等）。
Value* toPropertyKey(IContext* ctx, Value* key, PropertyKey& out);

// —— 属性描述符转换（ES2024 §6.2.6 / §10.1.6.2）——
// 描述符对象 → 完整 Property：缺失字段按规范默认值（false / undefined）。
// 数据与访问器字段混用、get/set 非可调用 → TypeError。
Value* propertyDescriptorFromValue(IContext* ctx, Value* descObj, Property& out);
// Property → 描述符对象（Object.getOwnPropertyDescriptor / Proxy trap 参数）。
Value* propertyDescriptorToValue(IContext* ctx, const Property& p);
// IsCompatiblePropertyDescriptor(current, desc)：desc 能否应用到 current 上。
bool isCompatiblePropertyDescriptor(const Property& current, const Property& desc);

// —— 普通对象内部方法（ES2024 §10.1）——
// ProxyType 在 handler 无对应 trap 时回退到这些实现；ObjectType 的公开
// 属性操作也由它们组合而成。所有函数都逐跳经 IType 内部方法分派，
// 因此原型链上的 Proxy 会被正确触发。
//
// ordinaryGet（§10.1.8.1）：沿原型链读取，receiver 作为访问器 this。
Value* ordinaryGet(IContext* ctx, Value* obj, const PropertyKey& key, Value* receiver);
// ordinarySet（§10.1.9）：返回 Boolean（成功/失败）或 Exception/Fatal。
Value* ordinarySet(IContext* ctx, Value* obj, const PropertyKey& key, Value* value,
                   Value* receiver);
// ordinaryHas（§10.1.7.1）：自有 + 原型链。返回 Boolean 或 Exception/Fatal。
Value* ordinaryHas(IContext* ctx, Value* obj, const PropertyKey& key);
// ordinaryDelete（§10.1.10）：只操作自有属性。返回 Boolean 或 Exception/Fatal。
Value* ordinaryDelete(IContext* ctx, Value* obj, const PropertyKey& key);
// ordinaryGetPrototype（§10.1.2）：无原型返回 null。
Value* ordinaryGetPrototype(IContext* ctx, Value* obj);
// ordinarySetPrototype（§10.1.3）：成功返回 obj；非法/成环返回 Exception。
Value* ordinarySetPrototype(IContext* ctx, Value* obj, Value* proto);

// super 属性访问（ADR-047）：从 obj（homeObject 的原型）开始沿链查找，
// receiver 作为 getter/setter 的 this。get 跳过 receiver 自有属性；
// set 按 base 的描述符决定调用 setter 还是在 receiver 上定义自有属性。
Value* getPropertyWithReceiver(IContext* ctx, Value* obj, Value* key, Value* receiver);
Value* setPropertyWithReceiver(IContext* ctx, Value* obj, Value* key, Value* value,
                               Value* receiver);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_OBJECT_H
