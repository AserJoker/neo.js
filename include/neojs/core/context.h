/*
 * core 层：context（运行时根）。
 *
 * 遵循 docs/scope-redesign.md §1.4/§6/§4：
 *   - context 共享长寿命：rootScope/模块表/内置。
 *   - vm 每次模块执行/函数调用独立，不拥有 scope；scope 树归 context。
 *   - current_scope：当前作用域指针（vm 操作）。
 *   - value 唯一 owner 是 Gc（全局表）；scope 只持引用清单（scope.values）。
 *     创建即挂 current_scope（scope_ref++），栈中存的全部是借用值。
 *   - scope 退出只 scope_ref--，delete 由 Gc::collect 延迟执行（§4 修正）。
 *
 * M1 提供：value 工厂 + scope 树管理 + load/store/clone 语义。
 */

#ifndef NEOJS_CORE_CONTEXT_H
#define NEOJS_CORE_CONTEXT_H

#include <string>
#include <unordered_map>

#include "neojs/core/allocator.h"
#include "neojs/core/data.h"
#include "neojs/core/gc.h"
#include "neojs/core/scope.h"
#include "neojs/core/value.h"

namespace neojs {
namespace core {

struct Context : AllocatedObject {
  Context();
  ~Context();

  /* 全局根作用域：所有模块的 globalScope 挂在 rootScope 下。 */
  Scope *root_scope = nullptr;

  /* 当前作用域（vm 操作；进入函数/模块时切换）。 */
  Scope *current_scope = nullptr;

  /* value 生命周期管理器：唯一 owner（全局表）。 */
  Gc gc_;

  /* ---------- 运行时选项 ---------- */

  /* 选项是作用域属性（Scope::options_），沿 parent 链查找：
   * 进入作用域 EnableOption 设到 current_scope，scope 销毁自动消失
   * （生命周期 = 作用域生命周期，异常路径也安全）。
   * 顶层 Program 的选项挂 root_scope（脚本级严格；后续模块体系再迁到模块 globalScope）。
   * Context 只提供代理，实际存储/查找在 Scope。 */
  bool get_option(const std::string &name, bool dflt = false) const {
    return current_scope != nullptr ? current_scope->get_option(name, dflt)
                                    : dflt;
  }
  /* 设置选项到 current_scope（EnableOption 运行时调用）。 */
  void set_option(const std::string &name, bool value) {
    if (current_scope != nullptr) current_scope->set_option(name, value);
  }
  /* 当前 scope（不含父链）是否设置了某选项。 */
  bool has_option(const std::string &name) const {
    return current_scope != nullptr && current_scope->has_option_local(name);
  }

  /* ---------- value 工厂（创建经 gc，挂 current_scope，scope_ref++） ---------- */

  /* 创建 value（kind 指定），挂到 current_scope（scope_ref++）。
   * 返回原始 value*（调用方按借用持有，随作用域退出 scope_ref--）。 */
  Value *create_value(ValueKind kind);

  /* 带 data 的工厂：创建 value + data（data refcount=1）。 */
  Value *create_number(double n);
  Value *create_boolean(bool b);
  Value *create_string(const std::string &s);

  /* 特殊值工厂（无 data）。 */
  Value *create_undefined();
  Value *create_null();
  Value *create_uninitialized();

  /* 函数值工厂（M5-B）：value+FunctionData，挂到指定 closure scope。
   * bytecode/offset：函数体位置；param_count：形参个数；
   * params：形参名（常量池 idx，调用时参数绑定）。
   * 函数值本身挂 current_scope（定义点作用域），closure_scope 是
   * 函数内部查找外部变量的作用域（§7 调用序第一层）。 */
  Value *create_function(const vm::Bytecode *bytecode, size_t offset,
                         size_t param_count, Scope *closure_scope,
                         const std::vector<uint32_t> &params);

  /* 控制流中断工厂（§8.1）：创建 kind=Interrupt 的 value，
   * data=InterruptData{kind, value}（value 为借用指针，所有权在源 scope）。 */
  Value *create_interrupt(InterruptData::Kind kind, Value *value);

  /* 解包 interrupt：取 data 里的内部 value（借用指针）。
   * 仅当 v->kind == Interrupt 时调用，否则返回 v。 */
  Value *unwrap_interrupt(Value *v);

  /* ---------- scope 管理 ---------- */

  /* 在 current_scope 下新建子 scope 并设为 current_scope，返回新 scope。 */
  Scope *push_scope();

  /* 销毁 current_scope 并恢复 parent 为 current_scope。
   * 返回被销毁的 scope（调用方已不可用）。 */
  void pop_scope();

  /* 销毁指定 scope 整棵子树（递归 children + values 逐个 scope_ref--）。
   * 注意：只减引用计数，不 delete value（delete 由 Gc::collect 延迟）。 */
  void destroy_scope(Scope *scope);

  /* 将 scope 设为 current_scope（进入函数 body / 模块）。 */
  void set_current_scope(Scope *scope) { current_scope = scope; }

  /* 把已有 value 挂到指定 scope（scope_ref++），返回 value*。
   * 用于闭包捕获/模块 import 共享同一 value（§5/§10.7）。 */
  Value *attach(Scope *scope, Value *value);

  /* ---------- 绑定 ---------- */

  /* 在 current_scope 定义具名绑定：name → value*（所有权在 values）。
   * 已存在则覆盖（同名遮蔽由调用方保证）。 */
  void define(const std::string &name, Value *value);

  /* define 到 var 目标作用域（M5-A=root_scope；M5-B 函数内=函数级 scope）。
   * 供 var 声明提升使用（var 是函数作用域，非块作用域）。 */
  void define_var(const std::string &name, Value *value);

  /* 隐式全局：把 value 定义为全局（root_scope）绑定（非严格模式下
   * 赋值给未声明标识符时创建全局，JS 非严格语义）。
   * 返回该全局 value*（挂 root_scope，scope_ref++）。 */
  Value *define_global(const std::string &name, Value *value);

  /* load：沿 parent 链查找 named_values，返回原始变量（value*，非拷贝）。
   * 未找到返回 nullptr（全局兜底由调用方处理）。 */
  Value *load(const std::string &name);

  /* load_from：从指定 scope 沿 parent 链查找（闭包/模块 import 用）。 */
  Value *load_from(Scope *start, const std::string &name);

  /* store：改 data 内容，不顶替变量（§3.5）。
   *   绑定已存在：target->data = clone(new_value->data)，target 的 value
   *   对象本身不变 → 闭包 scope 持有的同一 value 读到新 data。
   *   绑定不存在：无操作（返回 false，由调用方决定隐式全局/ReferenceError）。
   * 返回 true 表示绑定存在且已存储。 */
  bool store(const std::string &name, Value *new_value);

  /* clone：按 data 类型分派（§3.5）。
   *   - 复杂变量（引用类型，有 refcount）：data->refcount++，返回同一 data。
   *   - 小对象（内联）：直接拷贝值。
   *   - 其他（值类型/需深拷贝）：深拷贝，new 新 data。
   * M1 值类型（Number/Boolean/String）走深拷贝；后续 Object 走 refcount。 */
  Data *clone(Data *data);
};

}  // namespace core
}  // namespace neojs

#endif  // NEOJS_CORE_CONTEXT_H
