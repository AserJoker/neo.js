// Module.h - ES Module 基础设施（模块标识 / 导出对象 / 加载器）
//
// 模块以**绝对路径**为唯一 id（JS 的 Module 以文件为单位，文件绝对路径全系统唯一）。
// Context 持有 id → Module 的映射；每个 Module 借用一棵以 global 为父的 root scope，
// 并持有一个**原型为 null** 的导出对象。
//
// import 的本质是「惰性加载 + 从 Context 取 Module + 从 Module 取导出对象」：
// 模块没有前端/字节码也依然存在——它只是一个 scope + 导出对象的载体，loader 负责
// 把导出写进导出对象。
//
// 关联 ADR: 011（Context 门面）、012（Scope 树）、044（可达性标记）

#ifndef NEO_CORE_MODULE_H
#define NEO_CORE_MODULE_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace neo::vm {
struct Code;
}  // namespace neo::vm

namespace neo::core {

class IContext;
class Scope;
class Value;

// ModuleLoader: 执行模块代码。exports 是模块的导出对象（原型为 null）。
// loader 在模块 root scope 下执行（current/root 均指向该 scope）。
// 成功返回非错误值（通常 undefined）；返回 Exception/Fatal 表示加载失败，
// importModule 原样透传该错误值。
using ModuleLoader = Value* (*)(IContext* ctx, Value* exports);

// 模块视图：root scope + 导出对象。二者均由 Context 持有，调用方只借用。
struct ModuleInfo {
    Scope* scope = nullptr;    // 模块 root scope（生命周期由 global 作用域树持有）
    Value* exports = nullptr;  // 导出对象（原型为 null）
};

// —— 编译产物的静态链接元数据（Compiler 产出，Context link 期消费）——

// import 声明：localName 绑定到 specifier 模块的 importName 槽位。
// namespaceImport=true 时 importName 为空，绑定到模块命名空间对象。
struct ModuleImportBinding {
    std::string localName;
    std::string specifier;
    std::string importName;
    bool namespaceImport = false;
};

// export 声明：
//   exportAll=true          —— export * from specifier（exportName 为空）
//   specifier 非空          —— re-export / export * as ns
//   specifier 为空          —— 本地绑定导出（localName）
struct ModuleExportBinding {
    std::string exportName;
    std::string localName;
    std::string specifier;
    std::string importName;
    bool exportAll = false;
};

// 一个模块的编译产物。code 由 Module 唯一持有，VM 只借用 const Code*。
// 三段式入口：
//   declarePc     —— 符号定义区（建槽；所有模块先声明，再 link）
//   instantiatePc —— 函数定义区（创建函数对象；此时 import 已绑定）
//   evaluatePc    —— 源码逻辑入口
struct ModuleCompilation {
    std::unique_ptr<vm::Code> code;
    uint32_t declarePc = 0;
    uint32_t instantiatePc = 0;
    uint32_t evaluatePc = 0;
    std::vector<ModuleImportBinding> imports;
    std::vector<ModuleExportBinding> exports;

    ModuleCompilation();
    ~ModuleCompilation();
    ModuleCompilation(ModuleCompilation&&) noexcept;
    ModuleCompilation& operator=(ModuleCompilation&&) noexcept;
    ModuleCompilation(const ModuleCompilation&) = delete;
    ModuleCompilation& operator=(const ModuleCompilation&) = delete;
};

}  // namespace neo::core

#endif  // NEO_CORE_MODULE_H
