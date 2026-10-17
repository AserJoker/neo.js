#ifndef NEOJS_RUNTIME_SCOPE_H
#define NEOJS_RUNTIME_SCOPE_H

/*
 * 内部 C++ 实现：JSScope（运行时词法环境）+ JSBinding（结构化绑定）。
 *
 * 遵循 docs/scope-value.md：
 *   P1 作用域 = 词法环境（Environment Record），只负责 name->绑定 解析，不持有值生命周期。
 *   P3 绑定是结构化条目（JSBinding），携带 value/kind/initialized，支撑 const/TDZ/闭包。
 *   P4 环境链（parent）负责词法查找（对齐 ECMAScript [[OuterEnv]]）。
 *   P5 环境树（children）负责协程/异步挂起（#7/#8）：挂起时不 pop，子树保留。
 *
 * 值生命周期（P2）：由 JSContext 全局登记表管理（M0）/ GC 堆（M2+），
 * 作用域析构只销毁绑定关系，不 free 值（见 context.h）。
 *
 * 注意：JSScope/JSBinding 是引擎内部对象，不对外暴露（C API 只暴露
 * JSValue / JSContext / JSAllocator）。
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "neojs/runtime/allocator.h"

namespace neojs {

class JSValue;

/* 绑定类型（对照 ECMAScript 声明）。M0 只记录不校验（打地基，M2 启用语义）。 */
enum class BindingKind : uint8_t {
  Var,      /* var 声明（函数级） */
  Let,      /* let 声明（块级） */
  Const,    /* const 声明（块级，不可重新赋值） */
  Function, /* 函数声明 */
  Param,    /* 函数参数 */
  Catch,    /* catch 子句参数 */
};

/* 结构化绑定条目（P3）。 */
struct JSBinding {
  JSValue *value = nullptr; /* 当前值；未初始化时为 nullptr */
  BindingKind kind = BindingKind::Var;
  bool initialized = true; /* TDZ 状态：let/const 声明后、初始化前为 false（M0 不校验） */
};

class JSScope {
 public:
  /* parent == nullptr 表示根作用域。构造时自动挂载到 parent 的 children。 */
  explicit JSScope(JSAllocator *allocator, JSScope *parent = nullptr);
  ~JSScope(); /* 只销毁绑定关系（P1），不 free 值（值归 ctx 登记表） */

  /* 作用域树访问（P4/P5） */
  JSScope *parent() const { return parent_; }
  const std::vector<JSScope *> &children() const { return children_; }

  /* 绑定查找与定义（P3/P4）：
   * findBindingScope 沿链（本环境 -> 父）解析，返回持绑定的环境；未找到返回 nullptr。
   * findBinding      沿链解析，返回绑定条目指针；未找到返回 nullptr。
   * defineBinding    覆盖式定义到**本环境**（不沿链，对应 JS_DefineValue）。 */
  JSScope *findBindingScope(const std::string &name);
  JSBinding *findBinding(const std::string &name);
  void defineBinding(const std::string &name, const JSBinding &binding);

 private:
  void addChild(JSScope *child);    /* 挂载子作用域（构造时调用） */
  void removeChild(JSScope *child); /* 摘除子作用域（子析构时调用） */

  JSAllocator *allocator_;                   /* 引用分配器（不拥有） */
  JSScope *parent_;                          /* 父作用域（根为 nullptr） */
  std::vector<JSScope *> children_;          /* 子作用域集合 */
  std::unordered_map<std::string, JSBinding> bindings_; /* name->绑定（P3） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_SCOPE_H
