#include "neojs/vm/vm.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include "neojs/engine/engine.h"

namespace neojs {
namespace vm {

namespace {

using engine::add;
using engine::bit_and;
using engine::bit_or;
using engine::bitwise_not;
using engine::bit_xor;
using engine::div;
using engine::eq;
using engine::ge;
using engine::gt;
using engine::le;
using engine::logical_not;
using engine::lt;
using engine::lshift;
using engine::mod;
using engine::mul;
using engine::ne;
using engine::pow;
using engine::rshift;
using engine::strict_eq;
using engine::strict_ne;
using engine::sub;
using engine::to_boolean;
using engine::typeof_value;
using engine::unary_minus;
using engine::unary_plus;
using engine::urshift;
using engine::void_value;

}  // namespace

/* ---------- 指令 handlers（每个 handler 自报操作数，自己推进 pc_） ----------
 * 返回值约定：
 *   - nullptr → 无产出（jump/pop）
 *   - 正常 value → run 循环 push 到操作数栈
 *   - Exception → run 循环透传返回
 *   - Interrupt（return）→ run 循环解包内部 value 返回 */

Value *Vm::h_push_const() {
  const ConstEntry &e = bc_->constants[cur().arg];
  Value *v = nullptr;
  switch (e.kind) {
    case ConstEntry::Kind::Number:
      v = engine::create_number(ctx_, e.number);
      break;
    case ConstEntry::Kind::String:
      v = engine::create_string(ctx_, e.str);
      break;
    case ConstEntry::Kind::Boolean:
      v = engine::create_boolean(ctx_, e.boolean);
      break;
    case ConstEntry::Kind::Null:
      v = engine::create_null(ctx_);
      break;
    case ConstEntry::Kind::Undefined:
      v = engine::create_undefined(ctx_);
      break;
  }
  next();
  return v;
}

Value *Vm::h_push_undefined() {
  next();
  return engine::create_undefined(ctx_);
}
Value *Vm::h_push_null() {
  next();
  return engine::create_null(ctx_);
}
Value *Vm::h_push_true() {
  next();
  return engine::create_boolean(ctx_, true);
}
Value *Vm::h_push_false() {
  next();
  return engine::create_boolean(ctx_, false);
}
Value *Vm::h_push_uninitialized() {
  next();
  return ctx_.create_uninitialized();
}

Value *Vm::h_load_var() {
  const std::string &name = bc_->constants[cur().arg].str;
  Context &c = ctx_;
  /* 全局内置常量：undefined / NaN / Infinity（恒存在，不依赖 scope） */
  if (name == "undefined") {
    next();
    return engine::create_undefined(c);
  }
  if (name == "NaN") {
    next();
    return engine::create_number(c, std::nan(""));
  }
  if (name == "Infinity") {
    next();
    return engine::create_number(c, std::numeric_limits<double>::infinity());
  }
  Value *v = c.load(name);
  if (v == nullptr) {
    /* 未定义变量：ReferenceError（异常 value），run 循环透传 */
    return engine::create_exception(
        c, "ReferenceError: " + name + " is not defined");
  }
  if (v->kind == core::ValueKind::Uninitialized) {
    /* TDZ：let/const 声明后未初始化时读取 → ReferenceError */
    return engine::create_exception(
        c, "ReferenceError: Cannot access '" + name + "' before initialization");
  }
  next();
  return v; /* 借用值：变量本身（scope 持有），栈不计数 */
}

Value *Vm::h_store_var() {
  const std::string &name = bc_->constants[cur().arg].str;
  Value *v = top(); /* 待存值（保留栈顶：赋值表达式结果为该值） */
  if (!ctx_.store(name, v)) {
    /* 绑定不存在：严格模式（"use strict" 选项启用）→ ReferenceError；
     * 非严格模式 → 隐式全局赋值（JS 非严格语义，define_global）。 */
    if (ctx_.get_option("use strict")) {
      return engine::create_exception(
          ctx_, "ReferenceError: " + name + " is not defined");
    }
    ctx_.define_global(name, v);
  }
  next();
  return nullptr; /* 值已留在栈顶（调用方按表达式结果处理） */
}

Value *Vm::h_define_var() {
  const std::string &name = bc_->constants[cur().arg].str;
  Value *v = pop(); /* 声明值（let/const 的 TDZ 哨兵） */
  ctx_.define(name, v);
  next();
  return nullptr;
}

Value *Vm::h_define_var_global() {
  const std::string &name = bc_->constants[cur().arg].str;
  Value *v = pop(); /* var 初始值 */
  ctx_.define_var(name, v); /* var 提升到 var 目标作用域（M5-A=全局） */
  next();
  return nullptr;
}

Value *Vm::h_push_scope() {
  ctx_.push_scope();
  next();
  return nullptr;
}

Value *Vm::h_pop_scope() {
  ctx_.pop_scope();
  next();
  return nullptr;
}

Value *Vm::h_pos() { return unary(unary_plus); }
Value *Vm::h_neg() { return unary(unary_minus); }
Value *Vm::h_not() { return unary(logical_not); }
Value *Vm::h_bitnot() { return unary(bitwise_not); }
Value *Vm::h_typeof() { return unary(typeof_value); }
Value *Vm::h_void() { return unary(void_value); }

Value *Vm::h_add() { return binary(add); }
Value *Vm::h_sub() { return binary(sub); }
Value *Vm::h_mul() { return binary(mul); }
Value *Vm::h_div() { return binary(div); }
Value *Vm::h_mod() { return binary(mod); }
Value *Vm::h_pow() { return binary(pow); }
Value *Vm::h_bitand() { return binary(bit_and); }
Value *Vm::h_bitor() { return binary(bit_or); }
Value *Vm::h_bitxor() { return binary(bit_xor); }
Value *Vm::h_shl() { return binary(lshift); }
Value *Vm::h_sar() { return binary(rshift); }
Value *Vm::h_shr() { return binary(urshift); }
Value *Vm::h_eq() { return binary(eq); }
Value *Vm::h_ne() { return binary(ne); }
Value *Vm::h_stricteq() { return binary(strict_eq); }
Value *Vm::h_strictne() { return binary(strict_ne); }
Value *Vm::h_lt() { return binary(lt); }
Value *Vm::h_le() { return binary(le); }
Value *Vm::h_gt() { return binary(gt); }
Value *Vm::h_ge() { return binary(ge); }

Value *Vm::h_jump() {
  pc_ = cur().arg;
  return nullptr;
}
Value *Vm::h_jump_if_true() {
  bool t = top_boolean();
  t ? jump_to(cur().arg) : next();
  return nullptr;
}
Value *Vm::h_jump_if_false() {
  bool t = top_boolean();
  t ? next() : jump_to(cur().arg);
  return nullptr;
}
Value *Vm::h_jump_if_nullish() {
  top_is_nullish() ? jump_to(cur().arg) : next();
  return nullptr;
}
Value *Vm::h_jump_if_not_nullish() {
  top_is_nullish() ? next() : jump_to(cur().arg);
  return nullptr;
}
Value *Vm::h_jump_if_true_pop() {
  bool t = top_boolean();
  pop(); /* 消费栈顶（条件值） */
  t ? jump_to(cur().arg) : next();
  return nullptr;
}
Value *Vm::h_jump_if_false_pop() {
  bool t = top_boolean();
  pop(); /* 消费栈顶（条件值） */
  t ? next() : jump_to(cur().arg);
  return nullptr;
}
Value *Vm::h_pop_op() {
  pop();
  next();
  return nullptr;
}

Value *Vm::h_ret() {
  /* §8.1 op_ret：pop → create_interrupt(return, val) → 返回 interrupt。
   * run 循环消费：解包内部 value 返回（当前仅 return 场景）。 */
  Value *v = pop();
  return ctx_.create_interrupt(core::InterruptData::Kind::Return, v);
}

Value *Vm::h_halt() {
  /* main 区结束：跳到指令结尾（阻止顺序流进函数体区）。 */
  pc_ = bc_->instructions.size();
  return nullptr;
}

Value *Vm::h_enable_option() {
  /* EnableOption{name}：启用选项，设到 current_scope（directive 序言，
   * 如 "use strict"）。选项是作用域属性：scope 销毁自动消失，
   * 沿 parent 链查找（嵌套作用域天然隔离，异常路径也安全）。 */
  const std::string &name = bc_->constants[cur().arg].str;
  ctx_.set_option(name, true);
  next();
  return nullptr;
}

Value *Vm::h_create_function() {
  /* CreateFunction{idx}：读函数表项，创建函数值压栈。
   * 函数值挂 current_scope（定义点作用域）；closure scope 独立创建
   * （parent=定义点作用域，不注册为 child——生命周期随函数值，
   * 定义点作用域销毁不连带销毁闭包）。pending_closure_ 供 SetClosure 挂载。 */
  const FunctionInfo &fi = bc_->functions[cur().arg];
  core::Scope *closure = new core::Scope();
  closure->parent = ctx_.current_scope;
  Value *func =
      ctx_.create_function(bc_, fi.offset, fi.param_count, closure, fi.params);
  next();
  pending_closure_ = static_cast<core::FunctionData *>(func->data);
  return func;
}

Value *Vm::h_set_closure() {
  /* SetClosure{name}：把栈顶捕获值挂到 pending 函数的 closure scope
   * （§10.8 op_set_closure：load 后 define 到函数 closure scope）。
   * 捕获共享同一 value（attach scope_ref++），读写穿透。 */
  const std::string &name = bc_->constants[cur().arg].str;
  Value *cap = pop(); /* LoadVar 压的捕获值 */
  if (pending_closure_ != nullptr && cap != nullptr) {
    core::Scope *cs = pending_closure_->closure_scope;
    ctx_.attach(cs, cap);
    cs->named_values[name] = cap;
  }
  next();
  return nullptr;
}

Value *Vm::h_call() {
  /* Call{argc}：函数调用（§7 调用序）。
   * 栈布局：[..., callee, arg1, ..., argN]，argc=N。
   * 1. 弹参数 + callee；校验 Function。
   * 2. 切 closure scope → 建 argument scope（参数绑定）→ 建 body scope。
   * 3. 新 vm 执行函数体（run_from 从 offset 开始）。
   * 4. 恢复调用方作用域，销毁两 scope，返回值重建挂调用方作用域。 */
  uint32_t argc = cur().arg;
  /* 防御：栈深不足（callee+args 不够）时立即终止，避免 size_t 下溢越界。 */
  if (stack_.size() < static_cast<size_t>(argc) + 1) {
    std::fprintf(stderr,
                 "neo.js: call underflow (stack=%zu, argc=%u)\n",
                 stack_.size(), argc);
    std::abort();
  }
  size_t callee_idx = stack_.size() - 1 - argc;
  Value *callee = stack_[callee_idx];
  core::FunctionData *fd = (callee != nullptr &&
                            callee->kind == core::ValueKind::Function)
                               ? static_cast<core::FunctionData *>(callee->data)
                               : nullptr;
  if (fd == nullptr) {
    /* 栈上残留 callee+args 需清理 */
    for (uint32_t i = 0; i <= argc; ++i) pop();
    next();
    return engine::create_exception(ctx_, "TypeError: not a function");
  }

  /* 弹参数（argN..arg1），弹 callee */
  std::vector<Value *> args;
  for (uint32_t i = 0; i < argc; ++i) args.push_back(pop());
  pop(); /* callee */

  /* §7 调用序 */
  core::Scope *saved = ctx_.current_scope;
  ctx_.set_current_scope(fd->closure_scope);   /* 1. 切闭包作用域 */
  core::Scope *arg_scope = ctx_.push_scope();  /* 2. argument scope */
  /* 参数绑定：形参名 → 实参 value（不足 undefined，多余忽略） */
  size_t nparams = fd->param_count;
  for (size_t i = 0; i < nparams; ++i) {
    Value *arg;
    if (i < args.size()) {
      arg = args[args.size() - 1 - i]; /* 反转：args 按 arg1..argN 收集 */
      ctx_.attach(arg_scope, arg);
    } else {
      arg = engine::create_undefined(ctx_); /* 挂 arg_scope（current_scope） */
    }
    if (i < fd->params.size()) {
      const std::string &pname =
          fd->bytecode->constants[fd->params[i]].str;
      if (!pname.empty()) arg_scope->named_values[pname] = arg;
    }
  }
  core::Scope *body_scope = ctx_.push_scope(); /* 3. body scope */
  (void)body_scope;

  /* 4. 新 vm 执行函数体（每调用新 vm，§6） */
  Vm callee_vm(ctx_);
  Value *res = callee_vm.run_from(*fd->bytecode, fd->offset);

  /* 5. 恢复调用方作用域，销毁两 scope */
  ctx_.set_current_scope(saved);
  ctx_.pop_scope(); /* body scope */
  ctx_.pop_scope(); /* argument scope */

  /* 6. 返回值重建：拷贝挂调用方作用域（§7），data 复用。
   * 异常也重建（StringData 深拷贝），避免随作用域销毁悬垂。 */
  Value *result = ctx_.create_value(res->kind);
  result->data = ctx_.clone(res->data);
  next();
  return result;
}

/* ---------- 执行辅助 ---------- */

Value *Vm::unary(Value *(*fn)(Context &, Value *)) {
  Value *v = pop();
  next();
  return fn(ctx_, v);
}

Value *Vm::binary(Value *(*fn)(Context &, Value *, Value *)) {
  Value *b = pop();
  Value *a = pop();
  next();
  return fn(ctx_, a, b);
}

bool Vm::top_boolean() {
  Value *v = top();
  return static_cast<core::BooleanData *>(to_boolean(ctx_, v)->data)->value;
}

bool Vm::top_is_nullish() {
  Value *v = top();
  return v->kind == core::ValueKind::Null ||
         v->kind == core::ValueKind::Undefined;
}

/* ---------- run：主循环，统一消费 handler 返回值 ---------- */

Value *Vm::run_from(const Bytecode &bc, size_t offset) {
  stack_.clear();
  bc_ = &bc;
  pc_ = offset;
  pending_closure_ = nullptr;

  while (pc_ < bc_->instructions.size()) {
    Value *r = (this->*kHandlers[static_cast<size_t>(cur().op)])();
    if (r == nullptr) continue;             /* 无产出指令（jump/pop） */
    if (engine::is_exception(r)) return r;  /* 异常透传 */
    if (engine::is_interrupt(r)) {
      return ctx_.unwrap_interrupt(r);      /* return → 解包内部 value */
    }
    push(r);                                /* 正常值压栈 */
  }

  /* 自然结束：返回栈顶（若空则 undefined） */
  if (stack_.empty()) return engine::create_undefined(ctx_);
  return top();
}

Value *Vm::run(const Bytecode &bc) { return run_from(bc, 0); }

}  // namespace vm
}  // namespace neojs
