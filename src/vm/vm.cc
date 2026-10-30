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

/* 执行器：每条指令一个 handler，经函数指针静态表分发（替代巨型 switch）。
 * handler 返回 true 继续执行，false 中断（异常/Ret）。 */
class Executor {
 public:
  Executor(Vm &vm, const Bytecode &bc, size_t &pc)
      : vm_(vm), bc_(bc), pc_(pc) {}

  /* 当前指令（pc 指向）。 */
  const Instruction &cur() const { return bc_.instructions[pc_]; }

  /* ---------- 指令 handlers（公有，供分发表取址） ---------- */

  bool h_push_const() {
    const ConstEntry &e = bc_.constants[cur().arg];
    switch (e.kind) {
      case ConstEntry::Kind::Number:
        push(engine::create_number(ctx(), e.number));
        break;
      case ConstEntry::Kind::String:
        push(engine::create_string(ctx(), e.str));
        break;
      case ConstEntry::Kind::Boolean:
        push(engine::create_boolean(ctx(), e.boolean));
        break;
      case ConstEntry::Kind::Null:
        push(engine::create_null(ctx()));
        break;
      case ConstEntry::Kind::Undefined:
        push(engine::create_undefined(ctx()));
        break;
    }
    next();
    return true;
  }

  bool h_push_undefined() {
    push(engine::create_undefined(ctx()));
    next();
    return true;
  }
  bool h_push_null() {
    push(engine::create_null(ctx()));
    next();
    return true;
  }
  bool h_push_true() {
    push(engine::create_boolean(ctx(), true));
    next();
    return true;
  }
  bool h_push_false() {
    push(engine::create_boolean(ctx(), false));
    next();
    return true;
  }

  bool h_load_var() {
    const std::string &name = bc_.constants[cur().arg].str;
    Context &c = ctx();
    /* 全局内置常量：undefined / NaN / Infinity（恒存在，不依赖 scope） */
    if (name == "undefined") {
      push(engine::create_undefined(c));
      next();
      return true;
    }
    if (name == "NaN") {
      push(engine::create_number(c, std::nan("")));
      next();
      return true;
    }
    if (name == "Infinity") {
      push(engine::create_number(c, std::numeric_limits<double>::infinity()));
      next();
      return true;
    }
    Value *v = c.load(name);
    if (v == nullptr) {
      /* 未定义变量：ReferenceError（异常 value） */
      push(engine::create_exception(
          c, "ReferenceError: " + name + " is not defined"));
      return false; /* 中断：返回异常 */
    }
    push(v); /* 借用值：变量本身（scope 持有），栈不计数 */
    next();
    return true;
  }

  bool h_pos() { return unary(unary_plus); }
  bool h_neg() { return unary(unary_minus); }
  bool h_not() { return unary(logical_not); }
  bool h_bitnot() { return unary(bitwise_not); }
  bool h_typeof() { return unary(typeof_value); }
  bool h_void() { return unary(void_value); }

  bool h_add() { return binary(add); }
  bool h_sub() { return binary(sub); }
  bool h_mul() { return binary(mul); }
  bool h_div() { return binary(div); }
  bool h_mod() { return binary(mod); }
  bool h_pow() { return binary(pow); }
  bool h_bitand() { return binary(bit_and); }
  bool h_bitor() { return binary(bit_or); }
  bool h_bitxor() { return binary(bit_xor); }
  bool h_shl() { return binary(lshift); }
  bool h_sar() { return binary(rshift); }
  bool h_shr() { return binary(urshift); }
  bool h_eq() { return binary(eq); }
  bool h_ne() { return binary(ne); }
  bool h_stricteq() { return binary(strict_eq); }
  bool h_strictne() { return binary(strict_ne); }
  bool h_lt() { return binary(lt); }
  bool h_le() { return binary(le); }
  bool h_gt() { return binary(gt); }
  bool h_ge() { return binary(ge); }

  bool h_jump() {
    pc_ = cur().arg;
    return true;
  }
  bool h_jump_if_true() {
    bool t = top_boolean();
    t ? jump_to(cur().arg) : next();
    return true;
  }
  bool h_jump_if_false() {
    bool t = top_boolean();
    t ? next() : jump_to(cur().arg);
    return true;
  }
  bool h_jump_if_nullish() {
    top_is_nullish() ? jump_to(cur().arg) : next();
    return true;
  }
  bool h_jump_if_not_nullish() {
    top_is_nullish() ? next() : jump_to(cur().arg);
    return true;
  }
  bool h_pop_op() {
    pop();
    next();
    return true;
  }
  bool h_ret() { return false; } /* 中断：run 返回栈顶 */

 private:
  /* ---------- 栈与 pc 辅助 ---------- */

  Context &ctx() { return vm_.ctx(); }
  Value *pop() { return vm_.pop(); }
  void push(Value *v) { vm_.push(v); }
  Value *top() const { return vm_.top(); }
  void next() { ++pc_; }
  void jump_to(uint32_t target) { pc_ = target; }

  /* 栈顶真值判定（JumpIfTrue/False） */
  bool top_boolean() {
    Value *v = top();
    return static_cast<core::BooleanData *>(to_boolean(ctx(), v)->data)->value;
  }

  /* 栈顶是否 null/undefined（JumpIfNullish/NotNullish） */
  bool top_is_nullish() {
    Value *v = top();
    return v->kind == core::ValueKind::Null ||
           v->kind == core::ValueKind::Undefined;
  }

  /* 一元：弹栈 → engine 运算 → 压栈 */
  bool unary(Value *(*fn)(Context &, Value *)) {
    Value *v = pop();
    push(fn(ctx(), v));
    next();
    return true;
  }

  /* 二元：弹两个操作数 → engine 运算 → 压栈 */
  bool binary(Value *(*fn)(Context &, Value *, Value *)) {
    Value *b = pop();
    Value *a = pop();
    push(fn(ctx(), a, b));
    next();
    return true;
  }

  Vm &vm_;
  const Bytecode &bc_;
  size_t &pc_;
};

/* 指令分发表：按 Opcode 枚举顺序（与 bytecode.h 一致）。 */
using Handler = bool (Executor::*)();
constexpr Handler kHandlers[] = {
    /* PushConst */ &Executor::h_push_const,
    /* PushUndefined */ &Executor::h_push_undefined,
    /* PushNull */ &Executor::h_push_null,
    /* PushTrue */ &Executor::h_push_true,
    /* PushFalse */ &Executor::h_push_false,
    /* LoadVar */ &Executor::h_load_var,
    /* Pos */ &Executor::h_pos,
    /* Neg */ &Executor::h_neg,
    /* Not */ &Executor::h_not,
    /* BitNot */ &Executor::h_bitnot,
    /* Typeof */ &Executor::h_typeof,
    /* Void */ &Executor::h_void,
    /* Add */ &Executor::h_add,
    /* Sub */ &Executor::h_sub,
    /* Mul */ &Executor::h_mul,
    /* Div */ &Executor::h_div,
    /* Mod */ &Executor::h_mod,
    /* Pow */ &Executor::h_pow,
    /* BitAnd */ &Executor::h_bitand,
    /* BitOr */ &Executor::h_bitor,
    /* BitXor */ &Executor::h_bitxor,
    /* Shl */ &Executor::h_shl,
    /* Sar */ &Executor::h_sar,
    /* Shr */ &Executor::h_shr,
    /* Eq */ &Executor::h_eq,
    /* Ne */ &Executor::h_ne,
    /* StrictEq */ &Executor::h_stricteq,
    /* StrictNe */ &Executor::h_strictne,
    /* Lt */ &Executor::h_lt,
    /* Le */ &Executor::h_le,
    /* Gt */ &Executor::h_gt,
    /* Ge */ &Executor::h_ge,
    /* Jump */ &Executor::h_jump,
    /* JumpIfTrue */ &Executor::h_jump_if_true,
    /* JumpIfFalse */ &Executor::h_jump_if_false,
    /* JumpIfNullish */ &Executor::h_jump_if_nullish,
    /* JumpIfNotNullish */ &Executor::h_jump_if_not_nullish,
    /* Pop */ &Executor::h_pop_op,
    /* Ret */ &Executor::h_ret,
};
static_assert(sizeof(kHandlers) / sizeof(kHandlers[0]) ==
                  static_cast<size_t>(Opcode::Count),
              "handler table must cover every Opcode");

}  // namespace

Value *Vm::run(const Bytecode &bc) {
  stack_.clear();
  const auto &insns = bc.instructions;
  size_t pc = 0;
  Executor ex(*this, bc, pc);

  while (pc < insns.size()) {
    const Instruction &instr = insns[pc];
    if (!(ex.*kHandlers[static_cast<size_t>(instr.op)])()) break;
  }

  /* 自然结束 / Ret / 异常：返回栈顶（若空则 undefined） */
  if (stack_.empty()) return engine::create_undefined(ctx_);
  return top();
}

}  // namespace vm
}  // namespace neojs
