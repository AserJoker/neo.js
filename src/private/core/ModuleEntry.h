// Module.h (private) - Module 内部结构
//
// Context 以绝对路径为 key 持有 Module。Module 只借用 root scope 与导出对象
// （生命周期由 global 作用域树 / GC 管理），自身不拥有它们。
//
// 关联 ADR: 011（Context 门面）、012（Scope 树）、044（可达性标记）

#ifndef NEO_CORE_PRIVATE_MODULE_H
#define NEO_CORE_PRIVATE_MODULE_H

#include "core/Module.h"

#include <string>

namespace neo::core {

struct Module {
    std::string id;              // 唯一 id（绝对路径）
    std::string dirname;         // id 的目录部分（绝对路径），用于解析相对 import
    Scope* scope = nullptr;      // 模块 root scope（借用，挂在 global 下）
    Value* exports = nullptr;    // 导出对象（借用，原型为 null）
    ModuleLoader loader = nullptr;  // 模块代码（可为空：仅占位，无字节码）
    bool loaded = false;         // loader 是否已成功执行
    bool loading = false;        // 循环依赖保护：正在执行中
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_MODULE_H
