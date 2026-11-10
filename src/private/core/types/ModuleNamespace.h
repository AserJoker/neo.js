// types/ModuleNamespace.h - 模块命名空间 exotic 对象（ES2024 §10.4.6）
//
// 模块导出对象不是普通 Object：
//   - 原型为 null；
//   - 导出属性是**活绑定**：Property::value 直接持有模块作用域里的 Value*，
//     **不 clone**。模块作用域的 store 会原地替换该 Value 的 meta+data，
//     因此导入方读取导出属性时总能看到最新值；
//   - 导出属性不可写、不可配置（enumerable 为 true），外部不能赋值/删除/改访问器；
//   - 原型不可改。
//
// 内存安全：导出 Value 是模块顶层绑定，由模块 root scope 跟踪（从 global 树可达）；
// Property 只是"虚"的对象-字段关系，不额外持有生命周期，也不需要 clone。
//
// 对象布局复用 ObjectData（属性容器/GC 遍历），只替换类型与属性语义。
//
// 关联 ADR: 018（对象模型）、044（可达性标记）

#ifndef NEO_CORE_PRIVATE_TYPES_MODULE_NAMESPACE_H
#define NEO_CORE_PRIVATE_TYPES_MODULE_NAMESPACE_H

#include "core/types/Object.h"

namespace neo::core {

class ModuleNamespaceType : public ObjectType {
public:
    ModuleNamespaceType() : ObjectType(TypeKind::ModuleNamespace) {}
    const char* name() const override { return "module namespace"; }

    // 导出属性只读：外部赋值/删除/定义访问器/改原型均报 TypeError。
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;

    // 导出入口：直接存 Value*（不 clone），并强制 writable=false /
    // enumerable=true / configurable=false。已存在的同名导出更新指针（活绑定）。
    Value* defineProperty(IContext* ctx, Value* self, Value* key, Value* value,
                          bool writable, bool enumerable,
                          bool configurable) override;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_MODULE_NAMESPACE_H
