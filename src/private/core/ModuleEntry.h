// Module.h (private) - Module 内部结构
//
// Context 以绝对路径为 key 持有 Module。Module 只借用 root scope 与导出对象
// （生命周期由 global 作用域树 / GC 管理），自身不拥有它们。
//
// 关联 ADR: 011（Context 门面）、012（Scope 树）、044（可达性标记）

#ifndef NEO_CORE_PRIVATE_MODULE_H
#define NEO_CORE_PRIVATE_MODULE_H

#include "core/Module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neo::core {

// 模块求值状态（ECMA-262 §16.2.1.6.1.7）。
enum class ModuleStatus : uint8_t {
    Linked,           // 已链接，未求值
    Evaluating,       // 在 DFS 栈上求值中
    EvaluatingAsync,  // 异步求值中（顶层 await 或异步依赖未完成）
    Evaluated,        // 已求值（成功或失败）
};

// [[AsyncEvaluationOrder]] 哨兵。
inline constexpr uint64_t kModuleAsyncOrderUnset = 0;
inline constexpr uint64_t kModuleAsyncOrderDone = ~uint64_t{0};

struct Module {
    Module();
    ~Module();

    std::string id;              // 唯一 id（绝对路径）
    std::string dirname;         // id 的目录部分（绝对路径），用于解析相对 import
    Scope* scope = nullptr;      // 模块 root scope（借用，挂在 global 下）
    Value* exports = nullptr;    // 导出对象（借用，原型为 null）
    Value* importMeta = nullptr; // import.meta 对象（借用，首次访问时创建）
    ModuleLoader loader = nullptr;  // 原生 loader（可为空）
    ModuleCompilation compilation;  // 编译产物（可为空：仅占位/原生 loader）
    bool loaded = false;         // loader/编译产物是否已成功执行
    bool loading = false;        // 原生 loader 循环依赖保护：正在执行中
    bool declared = false;       // 符号定义区是否已执行（槽位已建）
    bool declaring = false;      // 循环依赖保护：正在声明
    bool instantiated = false;   // import/export 槽位与函数对象是否已建立
    bool instantiating = false;  // 循环依赖保护：正在实例化

    // —— 模块求值状态机（ADR-064）——
    ModuleStatus status = ModuleStatus::Linked;
    size_t dfsAncestorIndex = 0;
    Module* cycleRoot = nullptr;      // 所属 SCC 的根（非空）
    bool hasTLA = false;              // [[HasTLA]]：模块自身含顶层 await
    uint64_t asyncEvaluationOrder = kModuleAsyncOrderUnset;
    std::vector<Module*> asyncParentModules;  // 依赖本异步模块的父模块
    size_t pendingAsyncDependencies = 0;
    // 环根的顶层求值 capability（借用，挂模块 scope）。
    Value* topLevelPromise = nullptr;
    Value* topLevelResolve = nullptr;
    Value* topLevelReject = nullptr;
    Value* evaluationError = nullptr;     // 失败错误值（借用，挂模块 scope）
    Value* evaluationCoroutine = nullptr; // 顶层求值协程（借用，挂模块 scope）
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_MODULE_H
