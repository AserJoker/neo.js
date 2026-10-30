/*
 * vm 层：栈式解释器（M4.3）。
 *
 * 架构（docs/architecture.md §5.4 + scope-redesign.md §6）：
 *   - vm 解析字节码，逐条调用 engine 接口完成语义。
 *   - 操作数栈由 vm 持有（借用 value 指针，不计数）；scope 树归 ctx。
 *   - 临时 value 由 engine create 挂到 ctx 内部 scope（scope_ref++），
 *     栈中存的是借用值；随 scope 退出由 GC 回收。
 *
 * M4 范围：仅表达式（无函数调用/语句）。
 *
 * 约定：
 *   - run 执行字节码，返回栈顶值（表达式结果）。
 *   - 遇异常 value（engine 返回 Exception）立即中断并返回。
 */

#ifndef NEOJS_VM_VM_H
#define NEOJS_VM_VM_H

#include <vector>

#include "neojs/core/context.h"
#include "neojs/vm/bytecode.h"

namespace neojs {
namespace vm {

using core::Context;
using core::Value;

/* 栈式 VM：持操作数栈，读字节码调 engine。 */
class Vm {
 public:
  explicit Vm(Context &ctx) : ctx_(ctx) {}

  /* 执行字节码，返回栈顶结果（借用值，挂 ctx scope）。
   * 遇异常 value 立即返回该异常。 */
  Value *run(const Bytecode &bc);

  /* 操作数栈访问（测试/调试用）。 */
  size_t stack_size() const { return stack_.size(); }
  Value *top() const {
    return stack_.empty() ? nullptr : stack_.back();
  }

 private:
  Context &ctx_;
  std::vector<Value *> stack_;

  void push(Value *v) { stack_.push_back(v); }
  Value *pop() {
    Value *v = stack_.back();
    stack_.pop_back();
    return v;
  }
};

}  // namespace vm
}  // namespace neojs

#endif  // NEOJS_VM_VM_H
