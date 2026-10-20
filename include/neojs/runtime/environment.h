/*
 * 内部 C++ 实现：JSEnvironment（闭包词法环境，M2.5）。
 *
 * 遵循 docs/architecture.md §5.3/§5.4 函数模型：
 *   - 函数对象持有 [[Environment]]（闭包环境），指向创建它的词法环境。
 *   - 每次函数调用创建新的环境（参数绑定到 name），parent 指向函数
 *     [[Environment]]，形成环境链。
 *   - 编译期静态 upvalue 解析（depth, name）：LoadUpvalue/StoreUpvalue
 *     沿环境链上跳 depth 层，在目标环境绑定表中查 name。
 *
 * GC（docs/gc.md §4.1）：环境持有绑定值 + parent 环境，都是引用边，
 * 经 gcMarkRefs 交给 Heap 递归标记（闭包可被 GC 跟踪，循环引用安全）。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_ENVIRONMENT_H
#define NEOJS_RUNTIME_ENVIRONMENT_H

#include <string>
#include <unordered_map>

#include "neojs/runtime/gc_header.h"

namespace neojs {

class Heap;
class JSValue;

/* 闭包词法环境：name -> 值 的绑定表 + parent 环境链。 */
class JSEnvironment : public GCHeader {
 public:
  JSEnvironment(JSEnvironment *parent = nullptr) : parent_(parent) {}
  ~JSEnvironment() override = default;

  /* 父环境链（创建时的外层环境）。GC 引用边。 */
  JSEnvironment *parent() const { return parent_; }
  void setParent(JSEnvironment *p) { parent_ = p; }

  /* 绑定查找：本环境是否存在该绑定。 */
  bool hasBinding(const std::string &name) const {
    return bindings_.find(name) != bindings_.end();
  }

  /* 读取绑定（不存在返回 nullptr）。 */
  JSValue *getBinding(const std::string &name) const {
    auto it = bindings_.find(name);
    return it == bindings_.end() ? nullptr : it->second;
  }

  /* 写入绑定（不存在则新建）。 */
  void setBinding(const std::string &name, JSValue *value) {
    bindings_[name] = value;
  }

  /* 绑定表只读访问（GC 标记用）。 */
  const std::unordered_map<std::string, JSValue *> &bindings() const {
    return bindings_;
  }

  /* GC：绑定值 + parent 环境都是引用边。 */
  void gcMarkRefs(Heap *heap) override;

 private:
  JSEnvironment *parent_ = nullptr;
  std::unordered_map<std::string, JSValue *> bindings_;
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_ENVIRONMENT_H
