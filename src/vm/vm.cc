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
  next();
  return v; /* 借用值：变量本身（scope 持有），栈不计数 */
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

Value *Vm::run(const Bytecode &bc) {
  stack_.clear();
  bc_ = &bc;
  pc_ = 0;

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

}  // namespace vm
}  // namespace neojs
