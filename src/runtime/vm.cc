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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/iterator.h"
#include "neojs/runtime/operators.h"
#include "neojs/runtime/utf16.h"
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

/* M4.4h：沿原型链查找命名属性的描述符（own 或原型链），供
 * getPropertyValue/setPropertyValue 访问器分发。找到返回描述符指针，
 * 未找到返回 nullptr。 */
PropertyDescriptor *findPropertyDescriptor(JSObject *obj,
                                          const std::string &name) {
  for (JSObject *o = obj; o != nullptr; o = o->prototype()) {
    PropertyDescriptor *desc = o->getOwnProperty(name);
    if (desc != nullptr) return desc;
  }
  return nullptr;
}

/* M4.4h：[[Get]]（§9.1.8 / §7.3.1）：沿原型链查找属性并取值。
 * 数据属性直接返回值；访问器属性调用 getter（this=目标对象）；
 * getter 缺省 -> undefined。 */
ConversionResult getPropertyValue(JSContext *ctx, JSObject *target,
                                  const std::string &name) {
  PropertyDescriptor *desc = findPropertyDescriptor(target, name);
  if (desc == nullptr) return ok(ctx->createValue(Kind::Undefined));
  if (desc->type == PropertyDescriptor::Type::Accessor) {
    if (desc->getter == nullptr || !isCallable(desc->getter)) {
      return ok(ctx->createValue(Kind::Undefined)); /* 无 getter -> undefined */
    }
    return call(ctx, desc->getter, target->holder(), nullptr, 0);
  }
  return ok(desc->value != nullptr ? desc->value
                                   : ctx->createValue(Kind::Undefined));
}

/* M4.4h：[[Set]]（§9.1.9 / §7.3.4）：赋值语义。
 * - own 数据属性：writable=false -> TypeError；否则更新。
 * - 访问器属性：调 setter（this=目标对象）；setter 缺省 -> TypeError。
 * - 无 own 属性：沿原型链找访问器属性（继承的访问器优先）；
 *   否则新增到本对象。 */
ConversionResult setPropertyValue(JSContext *ctx, JSObject *target,
                                  const std::string &name, JSValue *value) {
  PropertyDescriptor *own = target->getOwnProperty(name);
  if (own != nullptr) {
    if (own->type == PropertyDescriptor::Type::Accessor) {
      if (own->setter == nullptr || !isCallable(own->setter)) {
        return fail("TypeError: Cannot set property which has only a getter");
      }
      JSValue *args[1] = {value};
      return call(ctx, own->setter, target->holder(), args, 1);
    }
    if (!own->writable) {
      return fail("TypeError: Cannot assign to read only property '" + name +
                  "'");
    }
    own->value = value;
    return ok(value);
  }
  /* 无 own：沿原型链找访问器属性（继承的 setter 优先于新增）。 */
  for (JSObject *o = target->prototype(); o != nullptr; o = o->prototype()) {
    PropertyDescriptor *d = o->getOwnProperty(name);
    if (d == nullptr) continue;
    if (d->type == PropertyDescriptor::Type::Accessor) {
      if (d->setter == nullptr || !isCallable(d->setter)) {
        return fail("TypeError: Cannot set property which has only a getter");
      }
      JSValue *args[1] = {value};
      return call(ctx, d->setter, target->holder(), args, 1);
    }
    break; /* 继承数据属性：忽略（新增 own） */
  }
  /* 新增到本对象。 */
  PropertyDescriptor d;
  d.value = value;
  target->setOwnProperty(name, d);
  return ok(value);
}

/* 有符号十进制大数除法（BigInt / 与 %）：商向零截断，余数符号同被除数。
 * §6.1.6.2.7/§6.1.6.2.8。除零由调用方抛 RangeError（§6.1.6.2.4 步骤 4）。 */
ConversionResult bigintDivModSigned(JSContext *ctx, bool xNeg,
                                    const std::string &xDigits,
                                    bool yNeg, const std::string &yDigits,
                                    bool wantQuot) {
  std::string rem;
  std::string quot = bigintDivMod(xDigits, yDigits, rem);
  if (wantQuot) {
    bool outNeg = (xNeg != yNeg) && quot != "0";
    return ok(ctx->createValue(JSValue(outNeg, quot)));
  }
  /* 余数符号同被除数 x（§6.1.6.2.8）：-7n % 2n == -1n。 */
  bool outNeg = xNeg && rem != "0";
  return ok(ctx->createValue(JSValue(outNeg, rem)));
}

/* 二元算术（除字符串拼接外的数值运算）：ToNumeric 后运算。
 * BigInt 参与：两操作数必须同为 BigInt（混 Number -> TypeError，§6.1.6.2.4
 * 步骤 5）；结果 BigInt。Number 走 IEEE 浮点。 */
ConversionResult binaryArith(JSContext *ctx, JSValue *a, JSValue *b,
                             Opcode op) {
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  ConversionResult nb = toNumeric(ctx, b);
  if (!nb.ok()) return nb;
  JSValue *va = na.value;
  JSValue *vb = nb.value;

  if (va->kind() == Kind::BigInt || vb->kind() == Kind::BigInt) {
    /* §6.1.6.2.4 步骤 5：混用报 TypeError。 */
    if (va->kind() != vb->kind()) {
      return fail("TypeError: Cannot mix BigInt and other types, use explicit conversions");
    }
    bool xNeg = va->bigIntNegative();
    bool yNeg = vb->bigIntNegative();
    std::string xDigits = va->bigIntDigits();
    const std::string &yDigits = vb->bigIntDigits();
    switch (op) {
      case Opcode::Sub: {
        bool outNeg = false;
        bigintSubSigned(xNeg, xDigits, yNeg, yDigits, outNeg);
        return ok(ctx->createValue(JSValue(outNeg, xDigits)));
      }
      case Opcode::Mul: {
        bool outNeg = false;
        bigintMulSigned(xNeg, xDigits, yNeg, yDigits, outNeg);
        return ok(ctx->createValue(JSValue(outNeg, xDigits)));
      }
      case Opcode::Div: {
        if (yDigits == "0") return fail("RangeError: Division by zero");
        return bigintDivModSigned(ctx, xNeg, xDigits, yNeg, yDigits, true);
      }
      case Opcode::Mod: {
        if (yDigits == "0") return fail("RangeError: Division by zero");
        return bigintDivModSigned(ctx, xNeg, xDigits, yNeg, yDigits, false);
      }
      case Opcode::Pow: {
        if (yNeg) {
          /* 负指数 -> RangeError（§6.1.6.2.13 BigInt::exponentiate 步骤 4）。 */
          return fail("RangeError: Exponent must be positive");
        }
        bool expOdd = !yDigits.empty() && (yDigits.back() - '0') % 2 == 1;
        bigintPow(xDigits, yDigits);
        /* 结果符号：底负且指数奇才为负（(-2n)^2n == 4n）。 */
        return ok(ctx->createValue(JSValue(xNeg && expOdd && xDigits != "0",
                                           xDigits)));
      }
      default: return fail("Error: invalid binary arithmetic opcode");
    }
  }

  double x = va->toNumber();
  double y = vb->toNumber();
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

/* 十进制绝对值串 -> 二进制位串（最低位在前）。空串表示 0。 */
std::string bigintToBits(const std::string &digits) {
  std::string bits;
  std::string v = digits;
  while (v != "0") {
    unsigned rem = bigintDiv(v, 2);
    bits.push_back(rem ? '1' : '0');
  }
  return bits;
}

/* 位串（最低位在前）-> 十进制绝对值串。逐位累加（O(n·digits)，测试规模足够）。 */
std::string bitsToDecimal(const std::string &bits) {
  std::string out = "0";
  std::string place = "1"; /* 2^i */
  for (char c : bits) {
    if (c == '1') bigintAdd(out, place);
    bigintMul(place, 2);
  }
  return out;
}

/* 十进制绝对值串 + 符号 -> 指定宽度补码位串（最低位在前，宽度位，符号扩展）。 */
std::string twosComplementBits(const std::string &digits, bool neg, size_t width) {
  std::string bits = bigintToBits(digits);
  bits.resize(width, '0');
  if (neg) {
    /* 取反 + 1。 */
    for (auto &c : bits) c = (c == '0') ? '1' : '0';
    int carry = 1;
    for (size_t i = 0; i < width && carry; ++i) {
      if (bits[i] == '0') { bits[i] = '1'; carry = 0; }
      else { bits[i] = '0'; }
    }
  }
  return bits;
}

/* 补码位串（最低位在前，宽度位）-> 符号 + 绝对值十进制串。最高位为符号位。 */
void bitsToBigInt(const std::string &bits, bool &outNeg, std::string &outDigits) {
  size_t width = bits.size();
  bool neg = width > 0 && bits[width - 1] == '1';
  std::string mag = bits;
  if (neg) {
    for (auto &c : mag) c = (c == '0') ? '1' : '0';
    int carry = 1;
    for (size_t i = 0; i < width && carry; ++i) {
      if (mag[i] == '0') { mag[i] = '1'; carry = 0; }
      else { mag[i] = '0'; }
    }
  }
  outDigits = bitsToDecimal(mag);
  outNeg = neg;
  if (outDigits == "0") outNeg = false;
}

/* 十进制绝对值串左移 k 位（k >= 0）：abs * 2^k。 */
void bigintShiftLeftDecimal(std::string &digits, size_t k) {
  for (size_t i = 0; i < k; ++i) bigintMul(digits, 2);
}

/* 十进制绝对值串算术右移 k 位（k >= 0）：返回 floor(abs / 2^k)（截断）。 */
std::string bigintShiftRightDecimal(const std::string &digits, size_t k) {
  std::string v = digits;
  for (size_t i = 0; i < k; ++i) bigintDiv(v, 2);
  return v;
}

/* BigInt 算术右移 k 位（k >= 0，负数向负无穷）：结果 = x >> k。
 * 正数 -> floor(|x|/2^k)；负数 -> -(ceil(|x|/2^k))。返回绝对值串。 */
std::string bigintArithShiftRight(const std::string &digits, bool neg, size_t k) {
  if (digits == "0") return "0";
  if (!neg) return bigintShiftRightDecimal(digits, k);
  std::string divisor = "1";
  bigintShiftLeftDecimal(divisor, k);
  std::string rem;
  std::string quot = bigintDivMod(digits, divisor, rem);
  if (rem != "0") bigintAdd(quot, "1");
  return quot; /* ceil(|x|/2^k) */
}

/* BigInt 移位计数：十进制绝对值串 -> size_t（超界钳制到上限，防资源耗尽）。 */
size_t bigintShiftCount(const std::string &digits) {
  const size_t kMaxShift = 1u << 16; /* 65536 位：远超测试规模，防病态内存 */
  /* 逐位累积十进制 -> size_t，溢出即钳制。 */
  size_t n = 0;
  bool overflow = false;
  for (char c : digits) {
    unsigned d = static_cast<unsigned>(c - '0');
    if (n > (std::numeric_limits<size_t>::max() - d) / 10) { overflow = true; break; }
    n = n * 10 + d;
  }
  if (overflow || n > kMaxShift) return kMaxShift;
  return n;
}

/* BigInt 位运算（§6.1.6.2.14-19）：补码位串实现 & | ^；十进制快捷实现 << >>。 */
ConversionResult bigIntBitwise(JSContext *ctx, JSValue *x, JSValue *y,
                               Opcode op) {
  bool xNeg = x->bigIntNegative();
  bool yNeg = y->bigIntNegative();
  const std::string &xd = x->bigIntDigits();
  const std::string &yd = y->bigIntDigits();

  switch (op) {
    case Opcode::LShift: {
      /* a << b：b 为移位计数（可为负 -> 反向右移）。 */
      size_t k = bigintShiftCount(yd);
      std::string mag = xd;
      if (yNeg) {
        /* a << -k == a >> k（算术右移，负数向负无穷）。 */
        std::string shifted = bigintArithShiftRight(xd, xNeg, k);
        bool outNeg = xNeg && shifted != "0";
        return ok(ctx->createValue(JSValue(outNeg, shifted)));
      }
      bigintShiftLeftDecimal(mag, k);
      bool outNeg = xNeg && mag != "0";
      return ok(ctx->createValue(JSValue(outNeg, mag)));
    }
    case Opcode::RShift: {
      /* a >> b：算术右移（负数向负无穷）。b 为负 -> 反向左移。 */
      size_t k = bigintShiftCount(yd);
      if (yNeg) {
        std::string mag = xd;
        bigintShiftLeftDecimal(mag, k);
        bool outNeg = xNeg && mag != "0";
        return ok(ctx->createValue(JSValue(outNeg, mag)));
      }
      std::string shifted = bigintArithShiftRight(xd, xNeg, k);
      bool outNeg = xNeg && shifted != "0";
      return ok(ctx->createValue(JSValue(outNeg, shifted)));
    }
    default: break;
  }

  /* & | ^：补码位串按位运算。宽度 = 两操作数最大位长 + 2（符号扩展安全）。 */
  std::string xb = bigintToBits(xd);
  std::string yb = bigintToBits(yd);
  size_t width = std::max(xb.size(), yb.size()) + 2;
  std::string xbits = twosComplementBits(xd, xNeg, width);
  std::string ybits = twosComplementBits(yd, yNeg, width);
  std::string result(width, '0');
  for (size_t i = 0; i < width; ++i) {
    char xc = xbits[i];
    char yc = ybits[i];
    switch (op) {
      case Opcode::BitAnd: result[i] = (xc == '1' && yc == '1') ? '1' : '0'; break;
      case Opcode::BitOr: result[i] = (xc == '1' || yc == '1') ? '1' : '0'; break;
      case Opcode::BitXor: result[i] = (xc != yc) ? '1' : '0'; break;
      default: break;
    }
  }
  bool outNeg = false;
  std::string outDigits;
  bitsToBigInt(result, outNeg, outDigits);
  return ok(ctx->createValue(JSValue(outNeg, outDigits)));
}

/* 位运算（§13.9/§13.10/§13.11/§13.12）：ToNumeric 后，BigInt 走任意精度位运算
 * （混 Number -> TypeError；>>> 对 BigInt 抛 TypeError），否则 ToInt32 后按位运算。 */
ConversionResult bitwiseOp(JSContext *ctx, JSValue *a, JSValue *b, Opcode op) {
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  ConversionResult nb = toNumeric(ctx, b);
  if (!nb.ok()) return nb;
  JSValue *va = na.value;
  JSValue *vb = nb.value;
  if (va->kind() == Kind::BigInt || vb->kind() == Kind::BigInt) {
    /* §6.1.6.2.4 步骤 5：混用报 TypeError。 */
    if (va->kind() != vb->kind()) {
      return fail("TypeError: Cannot mix BigInt and other types, use explicit conversions");
    }
    /* §13.12.3 步骤 2：>>> 对 BigInt 抛 TypeError。 */
    if (op == Opcode::URShift) {
      return fail("TypeError: BigInts have no unsigned right shift, use >> instead");
    }
    return bigIntBitwise(ctx, va, vb, op);
  }
  int32_t x = toInt32Value(va->toNumber());
  int32_t y = toInt32Value(vb->toNumber());
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


/* 常量池值：按 Constant 载荷创建 Number / String / BigInt 值。 */
JSValue *constValue(JSContext *ctx, const FunctionBytecode &fn, uint16_t index) {
  if (index >= fn.constants.size()) {
    return ctx->createValue(Kind::Undefined);
  }
  const Constant &c = fn.constants[index];
  if (c.is_bigint) {
    /* BigInt 常量（M4.3）：十进制数字串 + 符号位。 */
    return ctx->createValue(JSValue(c.bigint_neg, c.bigint_digits));
  }
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
      case Opcode::PushBigInt:
        /* BigInt 常量（M4.3）：十进制数字串。 */
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
        /* M4.4：优先读环境绑定——内层闭包经 StoreUpvalue 更新外层局部后，
         * 外层 LoadVar 需读到最新值（顶层 var + forEach 回调场景）。
         * 环境无该绑定（如参数名未写入环境）时回退栈槽。 */
        if (env != nullptr && instr.arg < fn.locals.size() &&
            !fn.locals[instr.arg].empty()) {
          const std::string &name = fn.locals[instr.arg];
          JSValue *bv = env->getBinding(name);
          if (bv != nullptr) {
            stack.push_back(bv);
            break;
          }
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
      case Opcode::Dup: {
        /* 复制栈顶（M4.1 成员调用保留 this）。 */
        if (stack.size() <= fn.numLocals) {
          return fail("Error: stack underflow on Dup");
        }
        stack.push_back(stack.back());
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
        if (n.value->kind() == Kind::BigInt) {
          /* 一元取负（§13.5.4）：BigInt 翻转符号位，0n 仍为 0n。 */
          bool neg = !n.value->bigIntNegative();
          const std::string &digits = n.value->bigIntDigits();
          stack.push_back(ctx->createValue(
              JSValue(neg && digits != "0", digits)));
        } else {
          stack.push_back(ctx->createValue(-n.value->toNumber()));
        }
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
      case Opcode::JumpIfTrue: {
        /* M4.4f：栈顶为 true 则跳转（for-of 的 done 判断）。 */
        if (stack.empty()) return fail("Error: stack underflow on JumpIfTrue");
        JSValue *cond = stack.back(); stack.pop_back();
        if (toBoolean(cond)) pc = instr.arg;
        break;
      }
      case Opcode::JumpIfUndefined: {
        /* M5.1a：栈顶为 undefined 则跳转（解构默认值判断）。 */
        if (stack.empty()) return fail("Error: stack underflow on JumpIfUndefined");
        JSValue *v = stack.back(); stack.pop_back();
        if (v->kind() == Kind::Undefined) pc = instr.arg;
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
      case Opcode::CallMethod: {
        /* 成员方法调用（M4.1）：栈布局 obj, func, args...（argc 个）。
         * this = obj（func_slot 下方一位）。 */
        uint16_t argc = instr.arg;
        size_t func_slot = stack.size() - 1 - static_cast<size_t>(argc);
        if (func_slot <= fn.numLocals) {
          return fail("Error: stack underflow on CallMethod");
        }
        JSValue *this_obj = stack[func_slot - 1];
        JSValue *func = stack[func_slot];
        JSValue *const *call_args =
            argc > 0 ? &stack[func_slot + 1] : nullptr;
        ConversionResult r = call(ctx, func, this_obj, call_args, argc);
        if (!r.ok()) return r;
        /* 收缩：弹出 this + func + argc 个参数，压入结果。 */
        stack.resize(stack.size() - 2 - static_cast<size_t>(argc));
        stack.push_back(r.value);
        if (r.tmp_handle != 0) ctx->handles().free(r.tmp_handle);
        break;
      }
      case Opcode::Construct: {
        /* new 构造（M4.2）：栈布局 ctor, args...（argc 个）。
         * this = 新对象（construct 内部创建）。 */
        uint16_t argc = instr.arg;
        size_t ctor_slot = stack.size() - 1 - static_cast<size_t>(argc);
        if (ctor_slot < fn.numLocals) {
          return fail("Error: stack underflow on Construct");
        }
        JSValue *ctor = stack[ctor_slot];
        JSValue *const *call_args =
            argc > 0 ? &stack[ctor_slot + 1] : nullptr;
        ConversionResult r = construct(ctx, ctor, call_args, argc);
        if (!r.ok()) return r;
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
        /* 函数原型链 -> Function.prototype（M4.2）：Script 函数挂 call/apply/
         * bind 方法（§20.2.3）。惰性创建缓存，未注册内置时原型为 null。 */
        JSObject *fproto = nullptr;
        JSValue *fp = neojs::functionProto(ctx);
        if (fp != nullptr) fproto = fp->object();
        JSFunction *f = ctx->heap().allocGCObject<JSFunction>(
            fproto, FunctionKind::Script, nullptr /* func */);
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
        /* 原始值成员读取（§13.3.3 + §7.1.13）：先 ToObject 装箱查属性，
         * 方法调用时 this 仍为原始值（getValue 返回原始值）。undefined/null
         * 装箱失败 -> TypeError。 */
        JSObject *target = nullptr;
        if (obj->isObject()) {
          target = obj->object();
        } else if (obj->kind() == Kind::Undefined ||
                   obj->kind() == Kind::Null) {
          return fail("TypeError: Cannot read properties of " +
                      (obj->kind() == Kind::Undefined ? std::string("undefined")
                                                      : std::string("null")));
        } else {
          ConversionResult boxed = toObject(ctx, obj);
          if (!boxed.ok()) return boxed;
          target = boxed.value->object();
        }
        JSValue *val = nullptr;
        if (target->isArray() && key == "length") {
          /* 数组 length 属性（M4.1）：由 elements 存储推导，非存储属性。 */
          val = ctx->createValue(static_cast<double>(target->length()));
        } else if (target->isMap() && key == "size") {
          /* Map size 属性（M4.4b，§24.1.3.13）：由条目数推导，非存储属性。 */
          val = ctx->createValue(static_cast<double>(target->collectionSize()));
        } else if (target->isSet() && key == "size") {
          /* Set size 属性（M4.4c，§24.2.3.9）：由条目数推导，非存储属性。 */
          val = ctx->createValue(static_cast<double>(target->collectionSize()));
        } else if (target->isWrapper() &&
                   target->kind() == ObjectKind::String && key == "length") {
          /* String 包装对象/原始串的 length（§10.4.3.3）：非存储属性，
           * 由 [[StringData]] 的 UTF-16 code unit 数推导。 */
          JSValue *data = target->wrapperData();
          val = (data != nullptr && data->kind() == Kind::String)
                    ? ctx->createValue(
                          static_cast<double>(utf16Length(data->toString())))
                    : ctx->createValue(Kind::Undefined);
        } else {
          /* M4.4h：访问器感知读取（数据属性直接取值；访问器调 getter）。 */
          ConversionResult gv = getPropertyValue(ctx, target, key);
          if (!gv.ok()) return gv;
          val = gv.value;
        }
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
        if (obj->object()->isArray() && key == "length") {
          /* 数组 length 写入（M4.1）：截断/扩展元素存储。
           * M4.4h：若 defineProperty 已置 writable=false 则拒绝（§9.4.2.1
           * ArraySetLength 的 Reject）。 */
          PropertyDescriptor *len_desc =
              obj->object()->getOwnProperty("length");
          if (len_desc != nullptr &&
              len_desc->type == PropertyDescriptor::Type::Data &&
              !len_desc->writable) {
            return fail("TypeError: Cannot assign to read only property 'length'");
          }
          double d = val->toNumber();
          if (d < 0.0 || d != std::trunc(d) || d > 4294967295.0) {
            return fail("RangeError: Invalid array length");
          }
          obj->object()->resizeElements(static_cast<size_t>(d));
        } else {
          /* M4.4h：访问器感知写入（writable 校验 + setter 调用）。 */
          ConversionResult sr = setPropertyValue(ctx, obj->object(), key, val);
          if (!sr.ok()) return sr;
        }
        stack.resize(stack.size() - 2);
        stack.push_back(val); /* 赋值表达式值 = 新值 */
        break;
      }
      case Opcode::LoadElem: {
        /* obj, key -> 栈顶（替换 obj）。key 运行期 ToPropertyKey。 */
        if (stack.size() < 2) return fail("Error: stack underflow on LoadElem");
        JSValue *key = stack.back();
        JSValue *obj = stack[stack.size() - 2];
        /* 数组整数索引快速路径（架构 §5.2：arr[i] -> elements）。 */
        if (obj->isObject() && obj->object()->isArray() && key->isNumber()) {
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
        /* Symbol 属性键（M4.4d）：查 Symbol 属性表（沿原型链）。
         * 原始值（如 'ab'[Symbol.iterator]）先 ToObject 装箱（§13.3.3
         * Property Accessors 的 RequireObjectCoercible + ToObject）。 */
        if (pk.value->kind() == Kind::Symbol) {
          JSValue *symbol_target = obj;
          if (!obj->isObject()) {
            if (obj->kind() == Kind::Undefined || obj->kind() == Kind::Null) {
              return fail("TypeError: Cannot read properties of " +
                          (obj->kind() == Kind::Undefined
                               ? std::string("undefined")
                               : std::string("null")));
            }
            ConversionResult boxed = toObject(ctx, obj);
            if (!boxed.ok()) return boxed;
            symbol_target = boxed.value;
          }
          JSValue *val = symbol_target->object()->getSymbolProperty(pk.value);
          stack.resize(stack.size() - 2);
          stack.push_back(val != nullptr ? val : ctx->createValue(Kind::Undefined));
          break;
        }
        /* 字符串原始值/包装对象整数索引（§10.4.3.4 String exotic object）：
         * 虚拟索引属性，由 [[StringData]] 的 code unit 推导。 */
        if (pk.value->kind() == Kind::String &&
            (obj->kind() == Kind::String ||
             (obj->isObject() && obj->object()->kind() == ObjectKind::String))) {
          const std::string &s =
              obj->kind() == Kind::String
                  ? obj->toString()
                  : obj->object()->wrapperData()->toString();
          const std::string &idx = pk.value->toString();
          if (!idx.empty() &&
              idx.find_first_not_of("0123456789") == std::string::npos) {
            size_t len = utf16Length(s);
            size_t i = 0;
            /* 数字串转 size_t（防溢出：超长 -> 越界 undefined）。 */
            if (idx.size() <= 18) {
              i = static_cast<size_t>(std::strtoull(idx.c_str(), nullptr, 10));
            }
            if (i < len) {
              JSValue *val = ctx->createValue(
                  utf16Substring(s, i, i + 1)); /* 单 code unit 串 */
              stack.resize(stack.size() - 2);
              stack.push_back(val);
              break;
            }
          }
        }
        /* 原始值成员读取（§13.3.3 + §7.1.13）：先 ToObject 装箱查属性。 */
        JSObject *target = nullptr;
        if (obj->isObject()) {
          target = obj->object();
        } else if (obj->kind() == Kind::Undefined ||
                   obj->kind() == Kind::Null) {
          return fail("TypeError: Cannot read properties of " +
                      (obj->kind() == Kind::Undefined ? std::string("undefined")
                                                      : std::string("null")));
        } else {
          ConversionResult boxed = toObject(ctx, obj);
          if (!boxed.ok()) return boxed;
          target = boxed.value->object();
        }
        /* M4.4h：访问器感知读取（数据属性直接取值；访问器调 getter）。 */
        ConversionResult gv = getPropertyValue(ctx, target, pk.value->toString());
        if (!gv.ok()) return gv;
        JSValue *val = gv.value;
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
        /* Symbol 属性键（M4.4d）：写 Symbol 属性表。 */
        if (pk.value->kind() == Kind::Symbol) {
          obj->object()->setSymbolProperty(pk.value, val);
          stack.resize(stack.size() - 3);
          stack.push_back(val); /* 赋值表达式值 = 新值 */
          break;
        }
        /* M4.4h：访问器感知写入（writable 校验 + setter 调用）。 */
        ConversionResult sr =
            setPropertyValue(ctx, obj->object(), pk.value->toString(), val);
        if (!sr.ok()) return sr;
        stack.resize(stack.size() - 3);
        stack.push_back(val); /* 赋值表达式值 = 新值 */
        break;
      }
      case Opcode::GetIterator: {
        /* 栈顶对象 -> 迭代器（M4.4f，§7.4.1）。 */
        if (stack.empty()) return fail("Error: stack underflow on GetIterator");
        ConversionResult it = getIterator(ctx, stack.back());
        if (!it.ok()) return it;
        stack.back() = it.value; /* 替换栈顶为迭代器 */
        break;
      }
      case Opcode::IteratorStep: {
        /* 栈顶 iterator -> [value, done]（done 在栈顶，M4.4f，§7.4.5）。
         * 完成时 value 为 undefined（规范 IteratorValue 未定义值）。
         * iteratorStep 完成返回 ok(nullptr)（成功但无值，§7.4.5 done=true），
         * 与失败（value=nullptr + error 非空）区分：仅 error 非空才真失败。 */
        if (stack.empty()) return fail("Error: stack underflow on IteratorStep");
        ConversionResult step = iteratorStep(ctx, stack.back());
        if (step.value == nullptr && !step.error.empty()) return step;
        if (step.value == nullptr) {
          /* 迭代完成：压 [undefined, true]。 */
          stack.back() = ctx->createValue(Kind::Undefined);
          stack.push_back(ctx->createValue(true));
        } else {
          ConversionResult v = iteratorValue(ctx, step.value);
          if (!v.ok()) return v;
          stack.back() = v.value; /* value */
          stack.push_back(ctx->createValue(false)); /* done=false */
        }
        break;
      }
      case Opcode::RestToArray: {
        /* M5.1a rest/展开：栈 [iterable, start] -> 剩余元素数组。
         * 迭代 iterable，跳过前 start 个元素，其余收集为新数组。
         * start 必须为非负整数（编译期保证）。 */
        if (stack.size() < 2) return fail("Error: stack underflow on RestToArray");
        JSValue *start_v = stack.back();
        JSValue *iterable = stack[stack.size() - 2];
        size_t start = start_v->isNumber()
                           ? static_cast<size_t>(std::max(0.0, start_v->toNumber()))
                           : 0;
        ConversionResult it = getIterator(ctx, iterable);
        if (!it.ok()) return it;
        JSValue *iterator = it.value;
        /* 跳过前 start 个。 */
        size_t skipped = 0;
        while (skipped < start) {
          ConversionResult step = iteratorStep(ctx, iterator);
          if (step.value == nullptr && !step.error.empty()) return step;
          if (step.value == nullptr) break; /* 迭代提前结束 */
          ++skipped;
        }
        /* 收集剩余元素。 */
        JSValue *arr = ctx->createArray(0);
        size_t idx = 0;
        for (;;) {
          ConversionResult step = iteratorStep(ctx, iterator);
          if (step.value == nullptr && !step.error.empty()) return step;
          if (step.value == nullptr) break; /* 迭代完成 */
          ConversionResult v = iteratorValue(ctx, step.value);
          if (!v.ok()) return v;
          arr->object()->setElement(idx++, v.value);
        }
        stack.resize(stack.size() - 2);
        stack.push_back(arr);
        break;
      }
      case Opcode::RestParam: {
        /* M5.1a rest 参数：args[k..] 收集为新数组压栈。
         * runBytecode 入口参数（args/argc）在本帧仍有效（调用栈递归
         * 每帧独立）。数组经 createArray 创建后立即入栈（根集保护）。 */
        size_t k = instr.arg;
        JSValue *arr = ctx->createArray(0);
        size_t idx = 0;
        for (size_t i = k; i < argc; ++i) {
          if (args[i] != nullptr) arr->object()->setElement(idx++, args[i]);
        }
        stack.push_back(arr);
        break;
      }
      case Opcode::AppendElem: {
        /* M5.1a 数组展开：弹栈值追加到槽内数组末尾（动态索引）。 */
        if (stack.empty() || instr.arg >= fn.numLocals) {
          return fail("Error: stack underflow on AppendElem");
        }
        JSValue *val = stack.back();
        stack.pop_back();
        JSValue *arr = stack[instr.arg];
        if (arr == nullptr || !arr->isObject() || !arr->object()->isArray()) {
          return fail("TypeError: AppendElem target is not an array");
        }
        size_t len = arr->object()->length();
        arr->object()->setElement(len, val);
        break;
      }
      case Opcode::AppendSpread: {
        /* M5.1a 数组展开：弹栈可迭代对象，其元素按序追加到槽内数组。
         * 迭代器入栈保护（循环期间 getIterator/iteratorStep/iteratorValue
         * 可能分配触发 GC，迭代器不在栈上会悬垂）。 */
        if (stack.empty() || instr.arg >= fn.numLocals) {
          return fail("Error: stack underflow on AppendSpread");
        }
        JSValue *iterable = stack.back();
        stack.pop_back();
        JSValue *arr = stack[instr.arg];
        if (arr == nullptr || !arr->isObject() || !arr->object()->isArray()) {
          return fail("TypeError: AppendSpread target is not an array");
        }
        ConversionResult it = getIterator(ctx, iterable);
        if (!it.ok()) return it;
        JSValue *iterator = it.value;
        stack.push_back(iterator); /* 迭代器入栈根集保护（循环期间） */
        for (;;) {
          ConversionResult step = iteratorStep(ctx, iterator);
          if (step.value == nullptr && !step.error.empty()) {
            stack.pop_back();
            return step;
          }
          if (step.value == nullptr) break; /* 迭代完成 */
          ConversionResult v = iteratorValue(ctx, step.value);
          if (!v.ok()) {
            stack.pop_back();
            return v;
          }
          size_t len = arr->object()->length();
          arr->object()->setElement(len, v.value);
        }
        stack.pop_back(); /* 弹出迭代器保护 */
        break;
      }
      case Opcode::CallArray: {
        /* M5.1a 展开调用：栈 [func, args_array] -> 调用结果。
         * args_array 元素（可能为 null=空洞）按序为参数。
         * func 与 args_array 保持在栈（GC 根集），flatten 到临时
         * vector 后调用（call 内部分配触发 GC，vector 内指针源自栈）。 */
        if (stack.size() < 2) return fail("Error: stack underflow on CallArray");
        JSValue *args_arr = stack.back();
        JSValue *func = stack[stack.size() - 2];
        if (args_arr == nullptr || !args_arr->isObject() ||
            !args_arr->object()->isArray()) {
          return fail("TypeError: spread argument must be iterable");
        }
        JSObject *aobj = args_arr->object();
        std::vector<JSValue *> flat;
        flat.reserve(aobj->length());
        for (size_t i = 0; i < aobj->length(); ++i) {
          JSValue *e = aobj->getElement(i);
          flat.push_back(e != nullptr ? e : ctx->createValue(Kind::Undefined));
        }
        JSValue *this_undef = ctx->createValue(Kind::Undefined);
        ConversionResult r =
            call(ctx, func, this_undef,
                 flat.empty() ? nullptr : flat.data(), flat.size());
        if (!r.ok()) return r;
        stack.resize(stack.size() - 2);
        stack.push_back(r.value);
        if (r.tmp_handle != 0) ctx->handles().free(r.tmp_handle);
        break;
      }
      case Opcode::ConstructArray: {
        /* M5.1a 展开 new：栈 [ctor, args_array] -> 构造实例。 */
        if (stack.size() < 2) return fail("Error: stack underflow on ConstructArray");
        JSValue *args_arr = stack.back();
        JSValue *ctor = stack[stack.size() - 2];
        if (args_arr == nullptr || !args_arr->isObject() ||
            !args_arr->object()->isArray()) {
          return fail("TypeError: spread argument must be iterable");
        }
        JSObject *aobj = args_arr->object();
        std::vector<JSValue *> flat;
        flat.reserve(aobj->length());
        for (size_t i = 0; i < aobj->length(); ++i) {
          JSValue *e = aobj->getElement(i);
          flat.push_back(e != nullptr ? e : ctx->createValue(Kind::Undefined));
        }
        ConversionResult r =
            construct(ctx, ctor, flat.empty() ? nullptr : flat.data(), flat.size());
        if (!r.ok()) return r;
        stack.resize(stack.size() - 2);
        stack.push_back(r.value);
        if (r.tmp_handle != 0) ctx->handles().free(r.tmp_handle);
        break;
      }
      case Opcode::CopyObjectProps: {
        /* M5.1a 对象展开 {…src}：弹栈源对象，own enumerable 字符串属性
         * 复制进槽内目标对象（CopyDataProperties，§10.1.2.1）。
         * 数组源：整数索引元素为 own enumerable 键。
         * 原始值源：ToObject 装箱后取属性（null/undefined 跳过）。 */
        if (stack.empty() || instr.arg >= fn.numLocals) {
          return fail("Error: stack underflow on CopyObjectProps");
        }
        JSValue *src = stack.back();
        stack.pop_back();
        JSValue *target = stack[instr.arg];
        if (target == nullptr || !target->isObject()) {
          return fail("TypeError: object spread target is not an object");
        }
        if (src == nullptr || src->kind() == Kind::Undefined ||
            src->kind() == Kind::Null) {
          break; /* null/undefined 源跳过（CopyDataProperties 步骤 2a） */
        }
        ConversionResult boxed = toObject(ctx, src);
        if (!boxed.ok()) return boxed;
        JSObject *sobj = boxed.value->object();
        JSObject *tobj = target->object();
        std::vector<std::string> keys;
        for (size_t i = 0; i < sobj->length(); ++i) {
          if (sobj->isArray() && sobj->hasElement(i)) keys.push_back(std::to_string(i));
        }
        if (sobj->kind() == ObjectKind::String) {
          JSValue *data = sobj->wrapperData();
          if (data != nullptr && data->kind() == Kind::String) {
            size_t len = utf16Length(data->toString());
            for (size_t i = 0; i < len; ++i) keys.push_back(std::to_string(i));
          }
        }
        for (const auto &name : sobj->propertyOrder()) {
          const PropertyDescriptor *d = sobj->getOwnProperty(name);
          if (d != nullptr && d->enumerable) keys.push_back(name);
        }
        for (const std::string &k : keys) {
          JSValue *v = nullptr;
          if (sobj->isArray()) {
            size_t idx = static_cast<size_t>(std::strtoull(k.c_str(), nullptr, 10));
            v = sobj->getElement(idx);
          } else {
            PropertyDescriptor *d = sobj->getOwnProperty(k);
            if (d != nullptr && d->type == PropertyDescriptor::Type::Accessor) {
              if (d->getter == nullptr || !isCallable(d->getter)) {
                v = ctx->createValue(Kind::Undefined);
              } else {
                ConversionResult r = call(ctx, d->getter, sobj->holder(), nullptr, 0);
                if (!r.ok()) return r;
                v = r.value;
              }
            } else {
              v = d != nullptr && d->value != nullptr ? d->value
                                                      : ctx->createValue(Kind::Undefined);
            }
          }
          if (v == nullptr) v = ctx->createValue(Kind::Undefined);
          bool numeric = !k.empty() &&
                         k.find_first_not_of("0123456789") == std::string::npos;
          if (tobj->isArray() && numeric && k.size() <= 18) {
            tobj->setElement(
                static_cast<size_t>(std::strtoull(k.c_str(), nullptr, 10)), v);
          } else {
            tobj->setProperty(k, v);
          }
        }
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
