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
#include <unordered_set>

#include "neojs/runtime/gc_header.h"

namespace neojs {

class Heap;
class JSValue;

/* 闭包词法环境：name -> 值 的绑定表 + parent 环境链。
 *
 * M5.5f 块作用域（§8.1.1 声明式环境记录，路径 A 规范式）：
 *   - 块环境（is_block_=true）由 PushBlockEnv 创建，插入 env 链，承载
 *     let/const 词法绑定。PopBlockEnv 恢复 frame.env 到父环境。
 *   - declareLexical 建立未初始化绑定（TDZ 占位，bindings_[name]=nullptr）；
 *     initBinding 写入值并清除 TDZ；LoadLexical 命中未初始化绑定抛
 *     ReferenceError（§9.4.3.1 step 4）；StoreLexical 命中 const 绑定抛
 *     TypeError（§9.4.3.2 step 6）。
 *   - 函数环境（is_block_=false）仍承载参数与 var（setBinding 普通写入）。 */
class JSEnvironment : public GCHeader {
 public:
  JSEnvironment(JSEnvironment *parent = nullptr) : parent_(parent) {}
  ~JSEnvironment() override = default;

  /* 父环境链（创建时的外层环境）。GC 引用边。 */
  JSEnvironment *parent() const { return parent_; }
  void setParent(JSEnvironment *p) { parent_ = p; }

  /* M5.5f：块环境标记（PushBlockEnv 创建的 let/const 承载环境）。 */
  void setBlock(bool b) { is_block_ = b; }
  bool isBlock() const { return is_block_; }

  /* 绑定查找：本环境是否存在该绑定。 */
  bool hasBinding(const std::string &name) const {
    return bindings_.find(name) != bindings_.end();
  }

  /* 读取绑定（不存在返回 nullptr；未初始化绑定亦为 nullptr）。 */
  JSValue *getBinding(const std::string &name) const {
    auto it = bindings_.find(name);
    return it == bindings_.end() ? nullptr : it->second;
  }

  /* 写入绑定（不存在则新建）。用于参数/var/块 let 已初始化后更新。 */
  void setBinding(const std::string &name, JSValue *value) {
    bindings_[name] = value;
  }

  /* M5.5f：声明词法绑定（let/const）——建立未初始化（TDZ）占位。
   * const 额外记入 immutable_（StoreLexical 拒绝写）。 */
  void declareLexical(const std::string &name, bool is_const) {
    bindings_[name] = nullptr; /* TDZ 占位（未初始化） */
    uninitialized_.insert(name);
    if (is_const) immutable_.insert(name);
  }

  /* M5.5f：初始化词法绑定（let x = v / const x = v）——写入值并清除 TDZ。 */
  void initBinding(const std::string &name, JSValue *value) {
    bindings_[name] = value;
    uninitialized_.erase(name);
  }

  /* M5.5f：TDZ 检查——绑定已声明但未初始化。 */
  bool isUninitialized(const std::string &name) const {
    return uninitialized_.find(name) != uninitialized_.end();
  }

  /* M5.5f：const 绑定检查（赋值抛 TypeError）。 */
  bool isImmutable(const std::string &name) const {
    return immutable_.find(name) != immutable_.end();
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
  /* M5.5f：块环境词法绑定元数据（TDZ 未初始化集合 + const 不可变集合）。
   * 仅块环境使用；普通环境（参数/var）两集合为空。 */
  std::unordered_set<std::string> uninitialized_;
  std::unordered_set<std::string> immutable_;
  bool is_block_ = false;
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_ENVIRONMENT_H
