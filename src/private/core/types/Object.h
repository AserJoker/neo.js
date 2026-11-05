// types/object.h - ObjectType + ObjectData + Property
//
// 普通对象（ADR-018 对象模型）：
//   properties —— hashmap（string key → Property，主存储）
//   order      —— vector<string>（只存 key 拷贝，保序遍历）
//   symbols    —— hashmap（symbol key → Property，Symbol 阶段启用，本阶段预留空容器）
//   prototype  —— 原型链指针（Value*，可空）
//
// Property 是属性描述符（ADR-018 §2）：value + writable/enumerable/configurable + getter/setter。
// 本阶段 getter/setter 字段预留但不调用（阶段 5 实现访问器属性）。
//
// GC 遍历（ADR-018 §5）：enumerateChildren 报告 prototype + 所有 property 的
// value/getter/setter（symbol 属性同理）。
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

    // 属性操作（ADR-018）
    Value* getProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* setProperty(IContext* ctx, Value* self, std::string_view key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* deleteProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* defineProperty(IContext* ctx, Value* self, std::string_view key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

class ObjectData : public IData {
public:
    std::unordered_map<std::string, Property> properties;  // 主存储
    std::vector<std::string> order;                        // 保序遍历（key 拷贝）
    // symbols：Symbol 类型未实现，本阶段预留空容器（后续启用）
    std::unordered_map<void*, Property> symbols;
    Value* prototype = nullptr;                            // 原型链指针（可空）

    // GC 遍历：报告 prototype + 所有 property 的 value/getter/setter
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this，不复制容器
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_OBJECT_H
