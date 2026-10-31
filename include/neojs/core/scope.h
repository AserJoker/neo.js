/*
 * core 层：scope（作用域）。
 *
 * 遵循 docs/scope-redesign.md §1.2/§4：
 *   - 每个 scope 持有：values(vec，所有权)、named_values(map，引用)、
 *     parent/children（作用域树，词法查找沿 parent 链）。
 *   - 作用域树代替栈变量；栈只用于操作数（栈中存的是借用值，不计数）。
 *   - delete scope：先从 parent 摘除自己 → 递归销毁所有 children → 遍历
 *     values 逐个 scope_ref--（归零的 value delete）。
 *   - value 的所有权永远在 scope 中。
 */

#ifndef NEOJS_CORE_SCOPE_H
#define NEOJS_CORE_SCOPE_H

#include <string>
#include <unordered_map>
#include <vector>

#include "neojs/core/allocator.h"
#include "neojs/core/value.h"

namespace neojs {
namespace core {

struct Scope : AllocatedObject {
  /* 作用域树 */
  Scope *parent = nullptr;
  std::vector<Scope *> children;

  /* values：当前作用域持有的 value 清单（所有权）。
   * 每个 value 进入时 scope_ref++，scope 销毁时逐个 --。 */
  std::vector<Value *> values;

  /* named_values：具名 value 的引用（所有权在 values）。
   * name → value*（同一 value 可能被多个 scope 共享：闭包/import）。 */
  std::unordered_map<std::string, Value *> named_values;

  /* 运行时选项（directive 序言："use strict" 等）。
   * 选项是作用域属性：进入作用域 EnableOption 设到本 scope，
   * 沿 parent 链向上查找（嵌套作用域天然隔离），scope 销毁自动消失
   * ——生命周期 = 作用域生命周期，异常/跳转路径也安全。 */
  std::unordered_map<std::string, bool> options_;

  /* 沿 parent 链查找选项（本 scope 起；不存在返回 dflt）。 */
  bool get_option(const std::string &name, bool dflt = false) const {
    for (const Scope *s = this; s != nullptr; s = s->parent) {
      auto it = s->options_.find(name);
      if (it != s->options_.end()) return it->second;
    }
    return dflt;
  }
  /* 设置选项到本 scope（EnableOption 运行时调用）。 */
  void set_option(const std::string &name, bool value) {
    if (value) {
      options_[name] = true;
    } else {
      options_.erase(name);
    }
  }
  /* 本 scope（不含父链）是否设置了某选项。 */
  bool has_option_local(const std::string &name) const {
    return options_.find(name) != options_.end();
  }

  Scope() = default;
  ~Scope() = default;
};

}  // namespace core
}  // namespace neojs

#endif  // NEOJS_CORE_SCOPE_H
