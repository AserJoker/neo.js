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

#include <cstddef>
#include <vector>

#include "neojs/core/context.h"
#include "neojs/vm/bytecode.h"

namespace neojs {
namespace vm {

using core::Context;
using core::Value;

/* 栈式 VM：持操作数栈 + 程序计数器，读字节码调 engine。
 * 约定（架构对齐 docs/scope-redesign.md §7/§8/§9）：
 *   - 每条指令一个 handler（成员函数），返回 Value*：
 *     - nullptr → 无产出（jump/pop）
 *     - 正常 value → run 循环 push 到操作数栈
 *     - Exception / Interrupt → run 循环检测到即中断（透传/解包）
 *   - pc_ 归 vm 持有：协程挂起打包整个 vm 时 pc 随 vm 保存，恢复直接续跑。
 *   - 各 handler 自己推进 pc_（读完自己的操作数/立即数后 ++pc_），
 *     可变宽指令由指令自己决定跳几个槽。 */
class Vm {
 public:
  explicit Vm(Context &ctx) : ctx_(ctx) {}

  /* 执行字节码，返回栈顶结果（借用值，挂 ctx scope）。
   * 返回值语义：
   *   - 异常（kind=Exception）→ 透传该异常 value
   *   - 控制流中断（kind=Interrupt，当前仅 return）→ 解包内部 value 返回
   *   - 自然结束 → 返回栈顶（栈空则 undefined） */
  Value *run(const Bytecode &bc);

  /* 操作数栈访问（测试/调试用）。 */
  Context &ctx() { return ctx_; }
  size_t stack_size() const { return stack_.size(); }
  Value *top() const {
    return stack_.empty() ? nullptr : stack_.back();
  }
  void push(Value *v) { stack_.push_back(v); }
  Value *pop() {
    Value *v = stack_.back();
    stack_.pop_back();
    return v;
  }

 private:
  /* ---------- 指令 handlers（分发表取址，见 vm.cc） ---------- */

  /* handler 类型：返回 Value*，nullptr=无产出；正常=压栈；
   * Exception/Interrupt=中断（见 run）。 */
  using Handler = Value *(Vm::*)();

  Value *h_push_const();
  Value *h_push_undefined();
  Value *h_push_null();
  Value *h_push_true();
  Value *h_push_false();
  Value *h_load_var();

  Value *h_pos();
  Value *h_neg();
  Value *h_not();
  Value *h_bitnot();
  Value *h_typeof();
  Value *h_void();

  Value *h_add();
  Value *h_sub();
  Value *h_mul();
  Value *h_div();
  Value *h_mod();
  Value *h_pow();
  Value *h_bitand();
  Value *h_bitor();
  Value *h_bitxor();
  Value *h_shl();
  Value *h_sar();
  Value *h_shr();
  Value *h_eq();
  Value *h_ne();
  Value *h_stricteq();
  Value *h_strictne();
  Value *h_lt();
  Value *h_le();
  Value *h_gt();
  Value *h_ge();

  Value *h_jump();
  Value *h_jump_if_true();
  Value *h_jump_if_false();
  Value *h_jump_if_nullish();
  Value *h_jump_if_not_nullish();
  Value *h_pop_op();
  Value *h_ret();

  /* 指令分发表：按 Opcode 枚举顺序，与 bytecode.h 一一对应。
   * static_assert 校验覆盖（见 vm.cc）。 */
  static inline constexpr Handler kHandlers[] = {
      /* PushConst */ &Vm::h_push_const,
      /* PushUndefined */ &Vm::h_push_undefined,
      /* PushNull */ &Vm::h_push_null,
      /* PushTrue */ &Vm::h_push_true,
      /* PushFalse */ &Vm::h_push_false,
      /* LoadVar */ &Vm::h_load_var,
      /* Pos */ &Vm::h_pos,
      /* Neg */ &Vm::h_neg,
      /* Not */ &Vm::h_not,
      /* BitNot */ &Vm::h_bitnot,
      /* Typeof */ &Vm::h_typeof,
      /* Void */ &Vm::h_void,
      /* Add */ &Vm::h_add,
      /* Sub */ &Vm::h_sub,
      /* Mul */ &Vm::h_mul,
      /* Div */ &Vm::h_div,
      /* Mod */ &Vm::h_mod,
      /* Pow */ &Vm::h_pow,
      /* BitAnd */ &Vm::h_bitand,
      /* BitOr */ &Vm::h_bitor,
      /* BitXor */ &Vm::h_bitxor,
      /* Shl */ &Vm::h_shl,
      /* Sar */ &Vm::h_sar,
      /* Shr */ &Vm::h_shr,
      /* Eq */ &Vm::h_eq,
      /* Ne */ &Vm::h_ne,
      /* StrictEq */ &Vm::h_stricteq,
      /* StrictNe */ &Vm::h_strictne,
      /* Lt */ &Vm::h_lt,
      /* Le */ &Vm::h_le,
      /* Gt */ &Vm::h_gt,
      /* Ge */ &Vm::h_ge,
      /* Jump */ &Vm::h_jump,
      /* JumpIfTrue */ &Vm::h_jump_if_true,
      /* JumpIfFalse */ &Vm::h_jump_if_false,
      /* JumpIfNullish */ &Vm::h_jump_if_nullish,
      /* JumpIfNotNullish */ &Vm::h_jump_if_not_nullish,
      /* Pop */ &Vm::h_pop_op,
      /* Ret */ &Vm::h_ret,
  };
  static_assert(sizeof(kHandlers) / sizeof(kHandlers[0]) ==
                    static_cast<size_t>(Opcode::Count),
                "handler table must cover every Opcode");

  /* ---------- 执行辅助 ---------- */

  /* 当前指令（pc_ 指向）。 */
  const Instruction &cur() const { return bc_->instructions[pc_]; }
  /* 推进到下一指令（handler 自报操作数消耗后调用）。 */
  void next() { ++pc_; }
  void jump_to(uint32_t target) { pc_ = target; }

  /* 一元：弹栈 → engine 运算 → 返回结果。 */
  Value *unary(Value *(*fn)(Context &, Value *));
  /* 二元：弹两个操作数 → engine 运算 → 返回结果。 */
  Value *binary(Value *(*fn)(Context &, Value *, Value *));

  /* 栈顶真值判定（JumpIfTrue/False）。 */
  bool top_boolean();
  /* 栈顶是否 null/undefined（JumpIfNullish/NotNullish）。 */
  bool top_is_nullish();

  Context &ctx_;
  std::vector<Value *> stack_;
  const Bytecode *bc_ = nullptr; /* 当前执行字节码（run 设置） */
  size_t pc_ = 0;                /* 程序计数器：归 vm 持有（§9 协程保存） */
};

}  // namespace vm
}  // namespace neojs

#endif  // NEOJS_VM_VM_H
