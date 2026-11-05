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
// 本阶段 getter/setter 字段预留但不调用（阶段 5 实现访问器属性）。
//
// GC 遍历（ADR-018 §5）：enumerateChildren 报告 prototype + 所有 property 的
// value/getter/setter，以及 symbol 属性持有的 symbol key Value（保活键）。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、016（Type 体系）

#ifndef NEO_CORE_PRIVATE_TYPES_OBJECT_H
#define NEO_CORE_PRIVATE_TYPES_OBJECT_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace neo::core {

class Value;

// 属性描述符（ADR-018 §2）
struct Property {
    Value* value = nullptr;   // 属性值（数据属性）
    bool writable = true;
    bool enumerable = true;
    bool configurable = true;
    Value* getter = nullptr;  // 访问器属性 getter（阶段 5 启用）
    Value* setter = nullptr;  // 访问器属性 setter（阶段 5 启用）
};

// symbol 属性描述符：额外持有 symbol key 本身（GC 需保活 key，且删除时需按身份比对）
struct SymbolProperty {
    Value* key = nullptr;     // symbol key（Value*，类型必为 Symbol）
    Property prop;
};

class ObjectType : public IType {
public:
    ObjectType() : IType(TypeKind::Object) {}
    const char* name() const override { return "object"; }

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
    bool hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

class ObjectData : public IData {
public:
    std::unordered_map<std::string, Property> properties;  // 字符串键主存储
    std::vector<std::string> order;                        // 字符串键保序遍历（key 拷贝）
    // symbol 键：按 SymbolData* 身份哈希（描述不参与），值携带 key Value 供 GC 保活
    std::unordered_map<const void*, SymbolProperty> symbols;
    std::vector<const void*> symbolOrder;                  // symbol 键保序遍历
    Value* prototype = nullptr;                            // 原型链指针（可空）

    // GC 遍历：报告 prototype + 所有 property 的 value/getter/setter
    // + symbol 属性的 key/setter/getter/value
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this，不复制容器
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_OBJECT_H
