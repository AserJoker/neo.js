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

#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/operators.h"
#include "neojs/runtime/value.h"

namespace neojs {

namespace {

/* runBytecode 栈根集 RAII 守卫：构造时入栈，析构时弹出。
 * 确保任何返回路径（含错误/Ret）都不残留指向局部栈的悬垂指针。 */
class VmStackRootGuard {
 public:
  VmStackRootGuard(JSContext *ctx, std::vector<JSValue *> *stack)
      : ctx_(ctx) {
    ctx_->vmStackPush(stack);
  }
  ~VmStackRootGuard() { ctx_->vmStackPop(); }
  VmStackRootGuard(const VmStackRootGuard &) = delete;
  VmStackRootGuard &operator=(const VmStackRootGuard &) = delete;

 private:
  JSContext *ctx_;
};

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
    case Opcode::Mod: return ok(ctx->createValue(std::fmod(x, y)));
    case Opcode::Pow: return ok(ctx->createValue(std::pow(x, y)));
    default: return fail("Error: invalid binary arithmetic opcode");
  }
}

/* ToInt32（§7.1.6）：取模 2^32 后转有符号 32 位。 */
int32_t toInt32Value(double d) {
  if (std::isnan(d) || std::isinf(d) || d == 0.0) return 0;
  double mod = std::fmod(std::trunc(d), 4294967296.0);
  if (mod < 0.0) mod += 4294967296.0; /* 模 2^32 得正余数 */
  uint32_t u = static_cast<uint32_t>(mod);
  return static_cast<int32_t>(u);
}

/* 位运算（§13.9/§13.10/§13.11/§13.12）：ToInt32 后按位运算。 */
ConversionResult bitwiseOp(JSContext *ctx, JSValue *a, JSValue *b, Opcode op) {
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  ConversionResult nb = toNumeric(ctx, b);
  if (!nb.ok()) return nb;
  int32_t x = toInt32Value(na.value->toNumber());
  int32_t y = toInt32Value(nb.value->toNumber());
  switch (op) {
    case Opcode::BitAnd: return ok(ctx->createValue(int64_t(x & y)));
    case Opcode::BitOr: return ok(ctx->createValue(int64_t(x | y)));
    case Opcode::BitXor: return ok(ctx->createValue(int64_t(x ^ y)));
    case Opcode::LShift: return ok(ctx->createValue(int64_t(static_cast<int32_t>(
        static_cast<uint32_t>(x) << (y & 31)))));
    case Opcode::RShift: return ok(ctx->createValue(int64_t(x >> (y & 31))));
    case Opcode::URShift: return ok(ctx->createValue(int64_t(static_cast<int32_t>(
        static_cast<uint32_t>(x) >> (y & 31)))));
    default: return fail("Error: invalid bitwise opcode");
  }
}

/* 比较指令分发：Eq -> abstractEquals；StrictEq -> strictEquals；
 * Lt -> isLessThan；Le/Gt/Ge 由 IsLessThan 组合；Ne/StrictNe 取反。
 * 返回 Boolean 值。 */
ConversionResult compareOp(JSContext *ctx, JSValue *a, JSValue *b, Opcode op) {
  switch (op) {
    case Opcode::Eq: return abstractEquals(ctx, a, b);
    case Opcode::StrictEq: return strictEquals(ctx, a, b);
    case Opcode::Lt: return isLessThan(ctx, a, b);
    case Opcode::Le: {
      /* a <= b == !(b < a)（§7.2.12 步骤：b < a 为 false 则 a <= b）。 */
      ConversionResult r = isLessThan(ctx, b, a);
      if (!r.ok()) return r;
      return ok(ctx->createValue(!r.value->toBoolean()));
    }
    case Opcode::Gt: {
      /* a > b == b < a。 */
      ConversionResult r = isLessThan(ctx, b, a);
      if (!r.ok()) return r;
      return ok(ctx->createValue(r.value->toBoolean()));
    }
    case Opcode::Ge: {
      /* a >= b == !(a < b)。 */
      ConversionResult r = isLessThan(ctx, a, b);
      if (!r.ok()) return r;
      return ok(ctx->createValue(!r.value->toBoolean()));
    }
    case Opcode::Ne: {
      ConversionResult r = abstractEquals(ctx, a, b);
      if (!r.ok()) return r;
      return ok(ctx->createValue(!r.value->toBoolean()));
    }
    case Opcode::StrictNe: {
      ConversionResult r = strictEquals(ctx, a, b);
      if (!r.ok()) return r;
      return ok(ctx->createValue(!r.value->toBoolean()));
    }
    default: return fail("Error: invalid compare opcode");
  }
}


/* 常量池值：按 Constant 载荷创建 Number 或 String 值。 */
JSValue *constValue(JSContext *ctx, const FunctionBytecode &fn, uint16_t index) {
  if (index >= fn.constants.size()) {
    return ctx->createValue(Kind::Undefined);
  }
  const Constant &c = fn.constants[index];
  if (c.is_number) {
    return ctx->createValue(c.number);
  }
  return ctx->createValue(c.str);
}

/* 按捕获解析目标环境（M2.5 闭包）：沿 env 链上跳 capture.depth + 1 层。
 * 编译器 depth 从当前函数的外层函数帧起计（0 = 直接外层），而 env 是
 * 当前函数自己的环境，故需先跳 1 层到创建本函数的环境，再跳 depth 层。
 * 返回目标环境；链不足/无环境返回 nullptr。 */
JSEnvironment *captureEnv(JSEnvironment *env, const FunctionBytecode &fn,
                          uint16_t idx) {
  if (idx >= fn.captures.size() || env == nullptr) return nullptr;
  JSEnvironment *e = env;
  size_t hops = static_cast<size_t>(fn.captures[idx].depth) + 1;
  for (size_t i = 0; i < hops && e != nullptr; ++i) {
    e = e->parent();
  }
  return e;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 执行入口                                                                    */
/* -------------------------------------------------------------------------- */

ConversionResult runBytecode(JSContext *ctx, const FunctionBytecode &fn,
                             JSValue *thisVal, JSValue *const *args,
                             size_t argc, JSEnvironment *env) {
  /* 操作数栈：栈底为局部变量槽（numLocals 个），之上为表达式求值栈。 */
  std::vector<JSValue *> stack;
  stack.reserve(static_cast<size_t>(fn.numLocals) + 64);

  /* 栈根集 RAII 守卫：构造入栈，析构（任意返回路径）弹出。
   * 嵌套 runBytecode（函数递归）会 push 多个栈帧，互不覆盖。
   * 之后写入入口参数（裸内部指针依赖根集保护）与局部占位。 */
  VmStackRootGuard stack_guard(ctx, &stack);

  /* 帧环境根集（M2.5 闭包）：env 链是捕获变量承载，与操作数栈同生命周期，
   * 帧进入注册、退出（任意返回路径）由 RAII 弹出。低阈值 GC 下 env
   * 若无根可达会被误回收（StoreVar/StoreUpvalue/captureEnv 悬垂崩溃）。 */
  class EnvRootGuard {
   public:
    EnvRootGuard(JSContext *ctx, JSEnvironment *env) : ctx_(ctx) {
      if (env != nullptr) ctx_->vmEnvPush(env);
    }
    ~EnvRootGuard() { ctx_->vmEnvPop(); }
    EnvRootGuard(const EnvRootGuard &) = delete;
    EnvRootGuard &operator=(const EnvRootGuard &) = delete;

   private:
    JSContext *ctx_;
  } env_guard(ctx, env);

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
        /* M2.5：局部变量同步写入当前环境（内层闭包 LoadUpvalue 沿链查找）。 */
        if (env != nullptr && instr.arg < fn.locals.size() &&
            !fn.locals[instr.arg].empty()) {
          env->setBinding(fn.locals[instr.arg], val);
        }
        stack.pop_back();       /* 弹出：赋值表达式不保留值在栈（链式赋值经 LoadVar） */
        break;
      }
      case Opcode::LoadGlobal: {
        /* 全局对象属性读取：LoadGlobal {name_index} -> 属性值（未定义属性返回 undefined）。 */
        if (instr.arg >= fn.constants.size() ||
            fn.constants[instr.arg].is_number) {
          return fail("Error: LoadGlobal requires a string constant");
        }
        const std::string &name = fn.constants[instr.arg].str;
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
      case Opcode::Div:
      case Opcode::Mod:
      case Opcode::Pow: {
        if (stack.size() < 2) return fail("Error: stack underflow on arith");
        JSValue *b = stack[stack.size() - 1];
        JSValue *a = stack[stack.size() - 2];
        ConversionResult r = binaryArith(ctx, a, b, instr.op);
        if (!r.ok()) return r;
        stack.resize(stack.size() - 2);
        stack.push_back(r.value);
        break;
      }
      case Opcode::BitAnd:
      case Opcode::BitOr:
      case Opcode::BitXor:
      case Opcode::LShift:
      case Opcode::RShift:
      case Opcode::URShift: {
        if (stack.size() < 2) return fail("Error: stack underflow on bitwise");
        JSValue *b = stack[stack.size() - 1];
        JSValue *a = stack[stack.size() - 2];
        ConversionResult r = bitwiseOp(ctx, a, b, instr.op);
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
      case Opcode::Not: {
        if (stack.empty()) return fail("Error: stack underflow on Not");
        JSValue *a = stack.back();
        bool b = toBoolean(a);
        stack.pop_back();
        stack.push_back(ctx->createValue(!b));
        break;
      }
      case Opcode::Typeof: {
        if (stack.empty()) return fail("Error: stack underflow on Typeof");
        JSValue *a = stack.back();
        ConversionResult r = typeOf(ctx, a);
        if (!r.ok()) return r;
        stack.pop_back();
        stack.push_back(r.value);
        break;
      }
      case Opcode::Eq:
      case Opcode::StrictEq:
      case Opcode::Lt:
      case Opcode::Le:
      case Opcode::Gt:
      case Opcode::Ge:
      case Opcode::Ne:
      case Opcode::StrictNe: {
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
        /* 收缩：弹出函数 + argc 个参数，压入结果。
         * 返回值保护句柄（call Script 分支分配，防 GC 回收窗口期）：
         * 先 push 入栈（受根集保护）再释放句柄，顺序安全。 */
        stack.resize(stack.size() - 1 - static_cast<size_t>(argc));
        stack.push_back(r.value);
        if (r.tmp_handle != 0) ctx->handles().free(r.tmp_handle);
        break;
      }
      case Opcode::Ret: {
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
      case Opcode::MakeFunction: {
        /* 从嵌套函数表实例化 JSFunction，[[Environment]] 绑定当前环境（M2.5）。
         * 嵌套函数字节码（fn.nested[arg]）复制给新函数。 */
        if (instr.arg >= fn.nested.size()) {
          return fail("Error: MakeFunction index out of range");
        }
        JSFunction *f = ctx->heap().allocGCObject<JSFunction>(
            nullptr /* prototype */, FunctionKind::Script, nullptr /* func */);
        f->setBytecode(fn.nested[instr.arg]); /* 复制（FunctionBytecode 值语义） */
        f->setEnvironment(env);
        ctx->heap().pin(f); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
        JSValue *v = ctx->heap().allocGCObject<JSValue>();
        v->setObject(f);
        f->setHolder(v);
        ctx->heap().unpin(f);
        stack.push_back(v);
        break;
      }
      case Opcode::NewObject: {
        /* 新建普通对象，原型链指向 Object.prototype（M3.3；
         * 惰性创建缓存，未注册内置时原型为 null）。 */
        JSValue *obj =
            ctx->createObject(objectProto(ctx) ? objectProto(ctx)->object()
                                               : nullptr);
        stack.push_back(obj);
        break;
      }
      case Opcode::NewArray: {
        /* 新建数组（空 elements，length = 0）。 */
        JSValue *arr = ctx->createArray(0);
        stack.push_back(arr);
        break;
      }
      case Opcode::LoadProp: {
        /* obj.key -> 栈顶（替换 obj）。key 为常量池字符串。 */
        if (stack.empty()) return fail("Error: stack underflow on LoadProp");
        JSValue *obj = stack.back();
        if (instr.arg >= fn.constants.size() ||
            fn.constants[instr.arg].is_number) {
          return fail("Error: LoadProp requires a string constant");
        }
        const std::string &key = fn.constants[instr.arg].str;
        if (!obj->isObject()) {
          return fail("TypeError: Cannot read properties of non-object");
        }
        JSValue *val = obj->object()->getProperty(key);
        stack.pop_back();
        stack.push_back(val != nullptr ? val : ctx->createValue(Kind::Undefined));
        break;
      }
      case Opcode::StoreProp: {
        /* obj, val -> 栈顶 val（obj.key = val）。key 为常量池字符串。
         * 消费 obj（编译器成员赋值后不再需要对象引用）。 */
        if (stack.size() < 2) return fail("Error: stack underflow on StoreProp");
        JSValue *val = stack.back();
        JSValue *obj = stack[stack.size() - 2];
        if (instr.arg >= fn.constants.size() ||
            fn.constants[instr.arg].is_number) {
          return fail("Error: StoreProp requires a string constant");
        }
        const std::string &key = fn.constants[instr.arg].str;
        if (!obj->isObject()) {
          return fail("TypeError: Cannot set properties of non-object");
        }
        obj->object()->setProperty(key, val);
        stack.resize(stack.size() - 2);
        stack.push_back(val); /* 赋值表达式值 = 新值 */
        break;
      }
      case Opcode::LoadElem: {
        /* obj, key -> 栈顶（替换 obj）。key 运行期 ToPropertyKey。 */
        if (stack.size() < 2) return fail("Error: stack underflow on LoadElem");
        JSValue *key = stack.back();
        JSValue *obj = stack[stack.size() - 2];
        if (!obj->isObject()) {
          return fail("TypeError: Cannot read properties of non-object");
        }
        /* 数组整数索引快速路径（架构 §5.2：arr[i] -> elements）。 */
        if (obj->object()->isArray() && key->isNumber()) {
          double d = key->toNumber();
          if (d >= 0.0 && d == std::trunc(d) && d < 4294967296.0) {
            JSValue *val = obj->object()->getElement(static_cast<size_t>(d));
            stack.resize(stack.size() - 2);
            stack.push_back(val != nullptr ? val : ctx->createValue(Kind::Undefined));
            break;
          }
        }
        ConversionResult pk = toPropertyKey(ctx, key);
        if (!pk.ok()) return pk;
        /* v1 只支持字符串属性名（Symbol 键留 M4）。 */
        if (pk.value->kind() == Kind::Symbol) {
          return fail("TypeError: symbol property keys not supported");
        }
        JSValue *val = obj->object()->getProperty(pk.value->toString());
        stack.resize(stack.size() - 2);
        stack.push_back(val != nullptr ? val : ctx->createValue(Kind::Undefined));
        break;
      }
      case Opcode::StoreElem: {
        /* obj, key, val -> 栈顶 val（obj[key] = val）。 */
        if (stack.size() < 3) return fail("Error: stack underflow on StoreElem");
        JSValue *val = stack.back();
        JSValue *key = stack[stack.size() - 2];
        JSValue *obj = stack[stack.size() - 3];
        if (!obj->isObject()) {
          return fail("TypeError: Cannot set properties of non-object");
        }
        if (obj->object()->isArray() && key->isNumber()) {
          double d = key->toNumber();
          if (d >= 0.0 && d == std::trunc(d) && d < 4294967296.0) {
            obj->object()->setElement(static_cast<size_t>(d), val);
            stack.resize(stack.size() - 3);
            stack.push_back(val);
            break;
          }
        }
        ConversionResult pk = toPropertyKey(ctx, key);
        if (!pk.ok()) return pk;
        if (pk.value->kind() == Kind::Symbol) {
          return fail("TypeError: symbol property keys not supported");
        }
        obj->object()->setProperty(pk.value->toString(), val);
        stack.resize(stack.size() - 3);
        stack.push_back(val); /* 赋值表达式值 = 新值 */
        break;
      }
      case Opcode::LoadUpvalue: {
        /* 捕获的外层变量读取（M2.5）：沿环境链 depth 跳转后查 name。 */
        JSEnvironment *e = captureEnv(env, fn, instr.arg);
        if (e == nullptr || instr.arg >= fn.captures.size()) {
          return fail("ReferenceError: captured variable not found");
        }
        const std::string &name = fn.captures[instr.arg].name;
        JSValue *val = e->getBinding(name);
        stack.push_back(val != nullptr ? val : ctx->createValue(Kind::Undefined));
        break;
      }
      case Opcode::StoreUpvalue: {
        /* 捕获的外层变量写入（M2.5）：沿环境链 depth 跳转后写回。 */
        if (stack.empty()) {
          return fail("Error: stack underflow on StoreUpvalue");
        }
        JSEnvironment *e = captureEnv(env, fn, instr.arg);
        if (e == nullptr || instr.arg >= fn.captures.size()) {
          return fail("ReferenceError: captured variable not found");
        }
        const std::string &name = fn.captures[instr.arg].name;
        JSValue *val = stack.back();
        stack.pop_back();
        e->setBinding(name, val); /* 值写回目标环境 */
        break;
      }
      case Opcode::Nop:
        break;
    }
  }

  /* 程序自然结束（无 Ret）：栈顶为结果；空栈 -> undefined。 */
  if (stack.empty()) return ok(ctx->createValue(Kind::Undefined));
  return ok(stack.back());
}

}  // namespace neojs
