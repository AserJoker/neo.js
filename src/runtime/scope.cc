#include "neojs/runtime/scope.h"

#include <algorithm>

#include "neojs/runtime/value.h"

namespace neojs {

JSScope::JSScope(JSAllocator *allocator, JSScope *parent)
    : allocator_(allocator), parent_(parent) {
  if (parent_ != nullptr) parent_->addChild(this);
}

JSScope::~JSScope() {
  /* 统一回收所有归属值（#5：作用域生命周期管理值；
   * #13：经 JSAllocator::destroy<T> 析构 + 释放，不泄漏值内资源）。 */
  for (auto &kv : values_) {
    allocator_->destroy(kv.first);
  }
  values_.clear();

  /* 递归回收未 pop 的子作用域（析构时顺带清理整棵子树，防泄漏）。 */
  for (JSScope *child : children_) {
    allocator_->destroy(child);
  }
  children_.clear();

  /* 从父作用域摘除自己。 */
  if (parent_ != nullptr) parent_->removeChild(this);
}

void JSScope::registerValue(JSValue *value) { values_[value] = true; }

void JSScope::unregisterValue(JSValue *value) { values_.erase(value); }

void JSScope::freeValue(JSValue *value) {
  if (value == nullptr) return;
  unregisterValue(value);
  allocator_->destroy(value); /* #13：析构 + 释放 */
}

void JSScope::addChild(JSScope *child) { children_.push_back(child); }

void JSScope::removeChild(JSScope *child) {
  auto it = std::find(children_.begin(), children_.end(), child);
  if (it != children_.end()) children_.erase(it);
}

void JSScope::setBinding(const std::string &name, JSValue *value) {
  bindings_[name] = value;
}

/* 沿作用域链（本作用域 -> 父）查找绑定；未找到返回 nullptr。 */
JSValue *JSScope::getBinding(const std::string &name) const {
  auto it = bindings_.find(name);
  if (it != bindings_.end()) return it->second;
  if (parent_ != nullptr) return parent_->getBinding(name);
  return nullptr;
}

}  // namespace neojs
