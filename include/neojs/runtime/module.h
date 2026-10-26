/*
 * 内部 C++ 实现：JSModuleRecord（模块记录，M5.2）。
 *
 * 遵循 docs/architecture.md §8 B4（模块/宿主）与 ECMA-262 §16.3（模块求值）：
 *   - 每个模块一条记录，由 JSContext 的模块表（specifier -> record）持有。
 *   - env 承载模块顶层词法环境（模块作用域隔离）：顶层 var/let/const/
 *     function/class 声明为模块求值函数的局部槽，经 op_LoadVar/op_StoreVar
 *     的 env 同步机制写入本 env；导出绑定 = env 绑定表项（live binding）。
 *   - imports 表：import 绑定的「本地名 -> 源模块记录 + 源导出名」映射。
 *     运行期 LoadImport 从源模块 env 读导出绑定（选项 A：动态查表，循环
 *     依赖天然支持）。
 *   - 状态机（§16.3.1.1）：Uninstantiated -> Instantiated -> Evaluating
 *     -> Evaluated。M5.2a 只实现 Uninstantiated/Evaluated 两端 + 依赖先
 *     求值；Instantiated/Evaluating 用于 M5.2b 循环依赖（Evaluating 标记
 *     使循环 import 返回未完成绑定）。
 *
 * GC（docs/gc.md §4.1）：env（模块环境，引用边）+ function（模块求值
 * 函数对象）经 gcMarkRefs 交给 Heap 递归标记。模块表由 JSContext::markRoots
 * 标记每个 record（架构 §7 根集「模块表」）。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_MODULE_H
#define NEOJS_RUNTIME_MODULE_H

#include <string>
#include <unordered_set>
#include <vector>

#include "neojs/compiler/bytecode.h"
#include "neojs/runtime/gc_header.h"

namespace neojs {

class Heap;
class JSEnvironment;
class JSFunction;
class JSModuleRecord;

/* 模块求值状态（ECMA-262 §16.3.1.1）：M5.2a 使用 Uninstantiated/Evaluated；
 * Evaluating 为 M5.2b 循环依赖预留（进入求值置位，循环 import 据此返回
 * 未完成绑定）。 */
enum class ModuleStatus : uint8_t {
  Uninstantiated,
  Instantiated,  /* M5.2b 预留 */
  Evaluating,    /* M5.2b 预留 */
  Evaluated,
};

/* 单条 import 绑定记录：本地名 -> （源模块记录, 源导出名）。
 * 编译期由 import 语句生成；运行期 LoadImport 据此查源模块 env。 */
struct ImportBinding {
  std::string local_name;   /* 本模块内的绑定名（局部槽名） */
  JSModuleRecord *from = nullptr; /* 源模块记录（模块表持有，GC 可达） */
  std::string export_name;  /* 源模块导出名 */
};

/* 单条 export 绑定记录：导出名 -> 本地绑定名（本模块 env 中的名字）。
 * 运行时 SetExport 把本地绑定值写入导出表；ReExport 转发自源模块。 */
struct ExportBinding {
  std::string export_name;  /* 导出名（"default" 为默认导出） */
  std::string local_name;   /* 本模块本地绑定名 */
  JSModuleRecord *from = nullptr; /* re-export 源模块记录（为空 = 本地导出） */
};

/* 模块记录（M5.2a）：specifier + 模块环境 + 导入/导出绑定表 + 状态 + 依赖。 */
class JSModuleRecord : public GCHeader {
 public:
  JSModuleRecord() = default;
  explicit JSModuleRecord(const std::string &specifier) : specifier_(specifier) {}
  ~JSModuleRecord() override = default;

  /* 模块标识（宿主回调的 specifier，原样保留）。 */
  const std::string &specifier() const { return specifier_; }

  /* 模块环境（顶层词法环境，作用域隔离）。GC 引用边。 */
  JSEnvironment *env() const { return env_; }
  void setEnv(JSEnvironment *env) { env_ = env; }

  /* 模块求值函数对象（Module Function，携带 FunctionBytecode）。
   * GC 引用边。 */
  JSFunction *function() const { return function_; }
  void setFunction(JSFunction *fn) { function_ = fn; }

  /* 模块求值函数字节码（Module Function 的指令，compileModule 产物）。
   * 与 JSFunction::bytecode() 相同载体；此处直接持有供 runBytecode 使用
   * （模块求值不走 call() 常规路径，见 module_loader.cc）。 */
  const FunctionBytecode &bytecode() const { return bc_; }
  void setFunctionBytecode(FunctionBytecode bc) { bc_ = std::move(bc); }

  /* 求值状态机。 */
  ModuleStatus status() const { return status_; }
  void setStatus(ModuleStatus s) { status_ = s; }

  /* 导入绑定表：import 语句编译时收集（LoadImport 运行期查表用）。 */
  const std::vector<ImportBinding> &imports() const { return imports_; }
  void addImport(ImportBinding b) { imports_.push_back(std::move(b)); }

  /* 导出绑定表：export 语句编译时收集（SetExport 运行期写导出表）。 */
  const std::vector<ExportBinding> &exports() const { return exports_; }
  void addExport(ExportBinding b) { exports_.push_back(std::move(b)); }

  /* 导出表：导出名 -> 值（live binding，与 env 绑定表同步更新）。
   * 运行期 SetExport 写入；LoadImport/ReExport 读取。 */
  const std::string *findLocalExport(const std::string &export_name) const;
  /* 解析导出名到最终本地绑定名（M5.2b）：本地导出 / 具名 re-export /
   * export * 合并，含环保护。返回 false = 导出不存在。 */
  bool findExport(const std::string &export_name, std::string *local_name,
                  JSModuleRecord **from) const;

  /* 依赖模块（import 语句的源模块记录，按源码序）。求值时先递归求值依赖。 */
  const std::vector<JSModuleRecord *> &deps() const { return deps_; }
  void addDep(JSModuleRecord *m) { deps_.push_back(m); }

  /* GC：模块环境 + 求值函数对象都是引用边。 */
  void gcMarkRefs(Heap *heap) override;

 private:
  /* findExport 的递归实现（带访问集环保护）。visit 跨调用共享。 */
  bool findExportRec(const std::string &export_name, std::string *local_name,
                     JSModuleRecord **from,
                     std::unordered_set<const JSModuleRecord *> &visit) const;

  std::string specifier_;          /* 模块标识 */
  JSEnvironment *env_ = nullptr;   /* 模块环境（顶层词法环境） */
  JSFunction *function_ = nullptr; /* 模块求值函数 */
  FunctionBytecode bc_;            /* 模块求值函数字节码（runBytecode 直入用） */
  ModuleStatus status_ = ModuleStatus::Uninstantiated;
  std::vector<ImportBinding> imports_; /* import 绑定表 */
  std::vector<ExportBinding> exports_; /* export 绑定表 */
  std::vector<JSModuleRecord *> deps_; /* 依赖模块（求值顺序） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_MODULE_H
