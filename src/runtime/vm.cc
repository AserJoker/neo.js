/*
 * 内部 C++ 实现：VM（字节码解释器核心）。
 *
 * 遵循 docs/architecture.md §5.4 执行模型：
 *   - 栈式 VM：操作数栈 + 调用栈帧（Frame{fn, pc, stack, locals}）。
 *   - 主循环 switch 分发；异常经 ConversionResult 传递（#3/#4）。
 *   - GC 根集：Frame 的操作数栈 + 局部变量槽经 ctx->markRoots 标记
 *     （用户确认“栈帧入 GC 根集”，见 vm.h）。
 *
 * 首期指令集（M2-VM 最小可跑）：常量/加载/算术/比较/跳转/函数调用返回/
 * print。函数调用复用 E3 Call 统一入口（function.h）。
 */

#include "neojs/runtime/vm.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/operators.h"
#include "neojs/runtime/value.h"

namespace neojs {

namespace {

ConversionResult ok(JSValue *v) {
  ConversionResult r;
  r.value = v;
  return r;
}

ConversionResult fail(const std::string &error) {
  ConversionResult r;
  r.error = error;
  return r;
}

/* 二元算术（除字符串拼接外的数值运算）：ToNumeric 后运算。 */
ConversionResult binaryArith(JSContext *ctx, JSValue *a, JSValue *b,
                             Opcode op) {
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  ConversionResult nb = toNumeric(ctx, b);
  if (!nb.ok()) return nb;
  double x = na.value->toNumber();
  double y = nb.value->toNumber();
  switch (op) {
    case Opcode::Sub: return ok(ctx->createValue(x - y));
    case Opcode::Mul: return ok(ctx->createValue(x * y));
    case Opcode::Div: return ok(ctx->createValue(x / y));
    default: return fail("Error: invalid binary arithmetic opcode");
  }
}

/* 比较指令分发：Eq -> abstractEquals；StrictEq -> strictEquals；
 * Lt -> isLessThan。返回 Boolean 值。 */
ConversionResult compareOp(JSContext *ctx, JSValue *a, JSValue *b, Opcode op) {
  switch (op) {
    case Opcode::Eq: return abstractEquals(ctx, a, b);
    case Opcode::StrictEq: return strictEquals(ctx, a, b);
    case Opcode::Lt: return isLessThan(ctx, a, b);
    default: return fail("Error: invalid compare opcode");
  }
}

/* 常量池值：字符串 -> JSValue；数字字面量经字符串暂存（v1 简化）。 */
JSValue *constValue(JSContext *ctx, const FunctionBytecode &fn, uint16_t index) {
  if (index >= fn.constants.size()) {
    return ctx->createValue(Kind::Undefined);
  }
  return ctx->createValue(fn.constants[index]);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 执行入口                                                                    */
/* -------------------------------------------------------------------------- */

ConversionResult runBytecode(JSContext *ctx, const FunctionBytecode &fn,
                             JSValue *thisVal, JSValue *const *args,
                             size_t argc) {
  /* 操作数栈：栈底为局部变量槽（numLocals 个），之上为表达式求值栈。 */
  std::vector<JSValue *> stack;
  stack.reserve(static_cast<size_t>(fn.numLocals) + 64);

  /* 先注册根集，再写入入口参数（裸内部指针依赖根集保护）与局部占位。 */
  ctx->vmStackRoot(&stack);
  size_t args_to_copy = argc < fn.numLocals ? argc : fn.numLocals;
  for (size_t i = 0; i < fn.numLocals; ++i) {
    if (i < args_to_copy && args[i] != nullptr) {
      stack.push_back(args[i]);
    } else {
      stack.push_back(ctx->createValue(Kind::Undefined)); /* 局部槽占位 */
    }
  }

  /* 当前帧：pc + 栈引用。v1 单帧（顶层程序），函数调用经 E3 Call 递归。 */
  size_t pc = 0;
  const std::vector<Instruction> &code = fn.code;

  /* 顶层入口的 this 绑定：v1 单帧模型暂未使用（Call 指令内部处理 this）。 */
  (void)thisVal;

  /* 注意：指令执行期间操作数保持栈上（不提前 pop），运算完成后才收缩——
   * 否则操作数弹出后不受根集保护，运算内部分配触发 GC 会被回收（悬垂）。 */
  while (pc < code.size()) {
    const Instruction &instr = code[pc++];
    switch (instr.op) {
      case Opcode::PushConst:
        stack.push_back(constValue(ctx, fn, instr.arg));
        break;
      case Opcode::PushUndefined:
        stack.push_back(ctx->createValue(Kind::Undefined));
        break;
      case Opcode::PushNull:
        stack.push_back(ctx->createValue(Kind::Null));
        break;
      case Opcode::PushTrue:
        stack.push_back(ctx->createValue(true));
        break;
      case Opcode::PushFalse:
        stack.push_back(ctx->createValue(false));
        break;
      case Opcode::PushInt:
        stack.push_back(ctx->createValue(static_cast<int64_t>(
            static_cast<int16_t>(instr.arg))));
        break;
      case Opcode::LoadVar: {
        if (instr.arg >= stack.size()) {
          return fail("ReferenceError: local slot out of range");
        }
        stack.push_back(stack[instr.arg]);
        break;
      }
      case Opcode::StoreVar: {
        if (stack.empty() || instr.arg >= stack.size()) {
          return fail("ReferenceError: local slot out of range");
        }
        JSValue *val = stack.back();
        stack[instr.arg] = val; /* 栈顶 -> 局部槽 */
        stack.pop_back();       /* 弹出：赋值表达式不保留值在栈（链式赋值经 LoadVar） */
        break;
      }
      case Opcode::LoadGlobal: {
        /* 全局对象属性读取：LoadGlobal {name_index} -> 属性值（未定义属性返回 undefined）。 */
        const std::string &name = fn.constants[instr.arg];
        JSValue *global = ctx->globalObject();
        JSValue *val = global->object()->getProperty(name);
        stack.push_back(val != nullptr ? val : ctx->createValue(Kind::Undefined));
        break;
      }
      case Opcode::Pop: {
        if (stack.size() <= fn.numLocals) {
          return fail("Error: stack underflow on Pop");
        }
        stack.pop_back();
        break;
      }
      case Opcode::Add: {
        /* 操作数保持栈上（GC 根集保护）：add 内部分配触发 GC 也不会回收操作数。 */
        if (stack.size() < 2) return fail("Error: stack underflow on Add");
        JSValue *b = stack[stack.size() - 1];
        JSValue *a = stack[stack.size() - 2];
        ConversionResult r = add(ctx, a, b);
        if (!r.ok()) return r;
        stack.resize(stack.size() - 2);
        stack.push_back(r.value);
        break;
      }
      case Opcode::Sub:
      case Opcode::Mul:
      case Opcode::Div: {
        if (stack.size() < 2) return fail("Error: stack underflow on arith");
        JSValue *b = stack[stack.size() - 1];
        JSValue *a = stack[stack.size() - 2];
        ConversionResult r = binaryArith(ctx, a, b, instr.op);
        if (!r.ok()) return r;
        stack.resize(stack.size() - 2);
        stack.push_back(r.value);
        break;
      }
      case Opcode::Neg: {
        if (stack.empty()) return fail("Error: stack underflow on Neg");
        JSValue *a = stack.back(); /* 保持栈上，toNumeric 完成后再收缩 */
        ConversionResult n = toNumeric(ctx, a);
        if (!n.ok()) return n;
        stack.pop_back();
        stack.push_back(ctx->createValue(-n.value->toNumber()));
        break;
      }
      case Opcode::Eq:
      case Opcode::StrictEq:
      case Opcode::Lt: {
        if (stack.size() < 2) return fail("Error: stack underflow on compare");
        JSValue *b = stack[stack.size() - 1];
        JSValue *a = stack[stack.size() - 2];
        ConversionResult r = compareOp(ctx, a, b, instr.op);
        if (!r.ok()) return r;
        stack.resize(stack.size() - 2);
        stack.push_back(r.value);
        break;
      }
      case Opcode::Jump: {
        pc = instr.arg;
        break;
      }
      case Opcode::JumpIfFalse: {
        if (stack.empty()) return fail("Error: stack underflow on JumpIfFalse");
        JSValue *cond = stack.back(); stack.pop_back();
        if (!toBoolean(cond)) pc = instr.arg;
        break;
      }
      case Opcode::Call: {
        uint16_t argc = instr.arg;
        size_t func_slot = stack.size() - 1 - static_cast<size_t>(argc);
        if (func_slot >= stack.size()) {
          return fail("Error: stack underflow on Call");
        }
        /* 函数与参数保持在栈上（GC 根集保护），仅以索引引用：
         * call() 内部分配临时句柄可能触发 GC，提前弹出会被回收（悬垂）。 */
        JSValue *func = stack[func_slot];
        JSValue *const *call_args =
            argc > 0 ? &stack[func_slot + 1] : nullptr;

        /* 调用（E3 统一入口；this 用 undefined，非严格函数绑到 global）。 */
        JSValue *this_undef = ctx->createValue(Kind::Undefined);
        ConversionResult r = call(ctx, func, this_undef, call_args, argc);
        if (!r.ok()) return r;
        /* 收缩：弹出函数 + argc 个参数，压入结果。 */
        stack.resize(stack.size() - 1 - static_cast<size_t>(argc));
        stack.push_back(r.value);
        break;
      }
      case Opcode::Ret: {
        ctx->vmStackRoot(nullptr); /* 解除栈根集保护 */
        if (stack.empty()) return ok(ctx->createValue(Kind::Undefined));
        JSValue *result = stack.back();
        return ok(result);
      }
      case Opcode::Print: {
        if (stack.empty()) return fail("Error: stack underflow on Print");
        JSValue *v = stack.back();
        /* toString 内部分配可能触发 GC：v 保持栈上受根集保护。 */
        if (v->kind() == Kind::String) {
          std::fputs(v->toString().c_str(), stdout);
        } else {
          ConversionResult s = toString(ctx, v);
          if (!s.ok()) return s;
          std::fputs(s.value->toString().c_str(), stdout);
        }
        std::fputs("\n", stdout);
        stack.pop_back();
        break;
      }
      case Opcode::Nop:
        break;
    }
  }

  /* 程序自然结束（无 Ret）：栈顶为结果；空栈 -> undefined。 */
  ctx->vmStackRoot(nullptr);
  if (stack.empty()) return ok(ctx->createValue(Kind::Undefined));
  return ok(stack.back());
}

}  // namespace neojs
