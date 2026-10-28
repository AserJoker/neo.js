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
#include <unordered_set>
#include <utility>
#include <vector>

#include "neojs/runtime/async.h"
#include "neojs/runtime/async_generator.h"
#include "neojs/runtime/async_iterator.h"
#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/generator.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/iterator.h"
#include "neojs/runtime/module_namespace.h"
#include "neojs/runtime/operators.h"
#include "neojs/runtime/promise.h"
#include "neojs/runtime/utf16.h"
#include "neojs/runtime/value.h"

namespace neojs {

namespace {

/* fail 定义于本匿名命名空间稍后（构造失败 ConversionResult）；前向声明。 */
ConversionResult fail(const std::string &error);

/* -------------------------------------------------------------------------- */
/* 分发表执行模型（M5.1e-f 栈帧瘦身）                                         */
/*                                                                             */
/* 背景：原巨型 switch-case 中 100+ 个 case 的局部变量在 Debug 模式下各占     */
/* 独立栈槽（编译器不复用），导致 runBytecode 单帧 19KB，递归 48 层即物理     */
/* 爆栈（远低于 maxCallDepth 默认 64）。                                      */
/*                                                                             */
/* 方案：每个 opcode 一个独立 handler 函数（独立小栈帧），主循环经全局静态    */
/* 分发表 kHandlers 按下标（Opcode 枚举值）查表调用。runBytecode 帧只剩       */
/* 循环控制变量，递归深度恢复到基线水平。                                     */
/*                                                                             */
/* handler 语义（统一签名 OpHandler = ConversionResult (*)(VmFrame&)）：      */
/*   - 成功：return ok(nullptr)。ok(nullptr) 的 value==nullptr 但 error 空、  */
/*     thrown==nullptr，主循环据此判定非失败（与 switch 尾部的错误检查一致）。*/
/*   - 错误：return fail(msg)（内部错误字符串）或 return 携带 thrown 的       */
/*     ConversionResult（JS throw 值）；主循环统一 tryDispatch 拦截/传播。    */
/*   - Ret：设 f.pc = f.fn.code.size() 使主循环退出（等价原 return ok）。     */
/*   - Jump/JumpIf*：改 f.pc 后 return ok(nullptr)。                          */
/*                                                                             */
/* 注意：handler 内不抛 C++ 异常，不分配根集外的裸指针；操作数保持栈上        */
/* （GC 根集保护），与 switch 版本语义一致。                                  */
/* -------------------------------------------------------------------------- */

/* 帧级执行状态：handler 共享的帧上下文（引用语义，直接改调用方状态）。 */
struct VmFrame {
  JSContext *ctx = nullptr;
  const FunctionBytecode *fn = nullptr;
  JSValue *thisVal = nullptr;
  JSValue *const *args = nullptr;
  size_t argc = 0;
  JSEnvironment *env = nullptr;
  JSFunction *self = nullptr;
  std::vector<JSValue *> *stack = nullptr; /* 操作数栈（含局部槽） */
  size_t *pc = nullptr;                    /* 指令指针 */
  JSValue **frame_this = nullptr;          /* 当前帧 this（SuperCall 可更新） */
  uint16_t arg = 0;                        /* 当前指令立即数 */
  Opcode op = Opcode::Nop;                 /* 当前指令 opcode（共享 handler 区分用） */
  /* M5.1e-f：finally 挂起异常（帧级状态，§14.16.4）。 */
  ConversionResult pending;
  bool has_pending = false;
  uint32_t pending_handle = 0;
  /* M5.5b：挂起的是 return completion（vs throw completion）。
   * try-finally 内 return / generator.return() 注入时置位：finally 末尾
   * RethrowPending 据此用 return value 完成函数（不抛错），实现 §13.10.5
   * return 穿透 finally 语义。pending.thrown 装 return value。 */
  bool pending_is_return = false;
  bool done = false; /* Ret 执行后置位：主循环应立即结束本帧 */
  /* M5.4 async：await 挂起（op_Await 遇未决 promise 置位）。
   * 置位后主循环结束，runBytecode 返回 suspended=true。 */
  bool suspended = false;

  /* 便捷访问。 */
  const std::vector<Instruction> &code() const { return fn->code; }
  JSValue *&stack_at(size_t i) { return (*stack)[i]; }
};

/* handler 函数类型。 */
using OpHandler = ConversionResult (*)(VmFrame &f);

/* M5.1e-f：异常表拦截（§14.16）。
 * 异常点 fault=pc-1 命中 try/catch 区间时：恢复栈 + 绑定 catch 槽 +
 * 跳 handler，返回 true；未命中返回 false（传播）。
 *   stack        操作数栈（恢复栈深度用 + catch 槽写入）
 *   err          挂起/传播的异常（JS throw 值或内部错误字符串）
 *   new_pc       输出：handler 位置
 *   pending/has_pending/pending_handle  finally 挂起异常状态（帧级）
 *   env          当前函数环境（catch 槽同步写环境，与 StoreVar 一致）
 *   is_return    M5.5b：return completion（vs throw completion）。
 *                return 不进 catch（return 不是异常），只穿透 finally；
 *                try 只有 catch 无 finally 时 return 直接完成（不拦截）。 */
bool tryDispatch(JSContext *ctx, const FunctionBytecode &fn, size_t pc,
                 std::vector<JSValue *> &stack, const ConversionResult &err,
                 size_t *new_pc, ConversionResult *pending,
                 bool *has_pending, uint32_t *pending_handle,
                 JSEnvironment *env, bool is_return = false,
                 bool *pending_is_return = nullptr) {
  if (fn.try_entries.empty()) return false;
  const size_t fault = pc - 1; /* 异常点 = 当前指令 */

  /* 选择覆盖 fault 的最内层 try/catch 区间（最窄区间优先）。
   * 嵌套 try 编译顺序：外层先添加 entry、内层后添加（vector 后部），
   * 但外层 try 的 try_end 覆盖整个内层区间；此处遍历选出区间最窄者，
   * 即最内层的保护。catch 区间内再抛 -> 跳过本 entry（传播给外层）。 */
  const TryEntry *best = nullptr;
  uint32_t best_span = 0xFFFFFFFFu;
  for (const TryEntry &e : fn.try_entries) {
    const bool in_try = fault >= e.try_start && fault < e.try_end;
    const bool in_catch =
        e.catch_start != TryEntry::kNoHandler &&
        fault >= e.catch_start && fault < e.catch_end;
    if (!in_try && !in_catch) continue;
    /* catch 块内抛出的异常：本 entry 不拦截（其 catch 已消费过），继续传播。
     * return completion 在 catch 块内同样不进本 entry 的 catch（catch 已消费过
     * 异常），但若 catch 块也在 finally 区间内则继续穿透——由下方 finally
     * 分支处理（return 命中 catch 区间时跳过 catch，继续找外层 finally）。 */
    if (in_catch) continue;
    const uint32_t span = e.try_end - e.try_start;
    if (span < best_span) {
      best_span = span;
      best = &e;
    }
  }
  if (best == nullptr) return false;
  const TryEntry &e = *best;

  /* return completion 不进 catch（return 不是异常）。
   * 若该 try 只有 catch 无 finally，return 不拦截——继续传播给外层
   * （可能命中外层 finally）。遍历找有 finally 的外层。 */
  if (is_return) {
    /* 重新查找：从最内层往外，找首个有 finally 且覆盖 fault 的 entry。 */
    const TryEntry *finally_best = nullptr;
    uint32_t finally_span = 0xFFFFFFFFu;
    for (const TryEntry &e2 : fn.try_entries) {
      const bool in_try = fault >= e2.try_start && fault < e2.try_end;
      const bool in_catch =
          e2.catch_start != TryEntry::kNoHandler &&
          fault >= e2.catch_start && fault < e2.catch_end;
      if (!in_try && !in_catch) continue;
      if (in_catch) continue;
      if (e2.finally_start == TryEntry::kNoHandler) continue;
      const uint32_t span = e2.try_end - e2.try_start;
      if (span < finally_span) {
        finally_span = span;
        finally_best = &e2;
      }
    }
    if (finally_best == nullptr) return false; /* 无 finally 覆盖：return 直接完成 */
    const TryEntry &fe = *finally_best;
    /* 恢复栈到局部槽区（丢弃表达式求值栈）。 */
    stack.resize(fn.numLocals);
    *pending = err;
    *has_pending = true;
    if (pending_is_return != nullptr) *pending_is_return = true;
    /* return value 用临时句柄保护（根集），RethrowPending 释放。 */
    if (err.thrown != nullptr && *pending_handle == 0) {
      *pending_handle = ctx->handles().alloc(err.thrown);
    }
    *new_pc = fe.finally_start;
    return true;
  }

  /* 恢复栈到局部槽区（丢弃表达式求值栈）。 */
    stack.resize(fn.numLocals);

    if (e.catch_start != TryEntry::kNoHandler) {
      /* try 区间 + 有 catch：绑定 catch 槽，消费异常。 */
      JSValue *exval = err.thrown;
      if (exval == nullptr) {
        /* 内部错误 -> Exception 值（含错误信息，§14.16.3 步骤 5-7）。 */
        exval = ctx->createValue(Kind::Exception);
        exval->setErrorInfo(err.error);
      }
      if (e.catch_slot != TryEntry::kNoCatchSlot &&
          e.catch_slot < fn.numLocals) {
        stack[e.catch_slot] = exval;
        if (env != nullptr && e.catch_slot < fn.locals.size() &&
            !fn.locals[e.catch_slot].empty()) {
          env->setBinding(fn.locals[e.catch_slot], exval);
        }
      }
      *new_pc = e.catch_start;
      return true;
    }

    /* 有 finally：挂起异常（try 无 catch 或 catch 再抛），跳 finally。
     * finally 执行后由 RethrowPending 重抛。 */
    if (e.finally_start != TryEntry::kNoHandler) {
      *pending = err;
      *has_pending = true;
      /* 挂起异常值（JS throw）用临时句柄保护（根集），RethrowPending 释放。
       * 内部错误（thrown==nullptr）是字符串，无需句柄。 */
      if (err.thrown != nullptr && *pending_handle == 0) {
        *pending_handle = ctx->handles().alloc(err.thrown);
      }
      *new_pc = e.finally_start;
      return true;
    }

  /* try 区间无 catch 无 finally：不可能（parser 保证至少一个）。 */
  return false;
}

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

/* 判断字符串是否为规范数组索引（canonical numeric string，§7.1.33）：
 * 非负整数十进制、无前导零（"0" 合法，"00" 不合法）。
 * 用于 String 包装对象虚拟索引属性（'abc'[0]）判定。 */
bool isCanonicalArrayIndex(const std::string &key) {
  if (key.empty()) return false;
  if (key.size() > 1 && key[0] == '0') return false; /* 前导零 */
  for (char c : key) {
    if (c < '0' || c > '9') return false;
  }
  return true;
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

/* M5.1e-c for-in 键枚举（§7.3.24 EnumerateObjectProperties v1 简化）：
 * 收集对象自身 + 沿原型链各层的 own enumerable string-keyed 键。
 * 去重：子类覆盖原型同名属性时只枚举一次（先访问到的 own 属性胜出，
 * 键集合去重，后续原型层同名键跳过）。
 * 顺序：own 整数索引升序 + own 字符串创建序（§9.1.12 [[OwnPropertyKeys]]），
 * 再沿原型链逐层追加。数组：elements 索引（含整数键）+ 额外字符串键。
 * 返回字符串键列表。 */
void enumerateObjectKeys(JSObject *obj, std::vector<std::string> *keys) {
  std::vector<std::string> seen; /* 已收集键（去重） */
  auto add_key = [&](const std::string &k) {
    for (const std::string &s : seen) {
      if (s == k) return; /* 已存在：跳过 */
    }
    seen.push_back(k);
    keys->push_back(k);
  };
  for (JSObject *o = obj; o != nullptr; o = o->prototype()) {
    if (o->isArray()) {
      /* 数组元素：整数索引（§10.4.2.4，非空洞）。 */
      for (size_t i = 0; i < o->length(); ++i) {
        if (o->hasElement(i)) add_key(std::to_string(i));
      }
    } else if (o->kind() == ObjectKind::String) {
      /* String exotic object（§10.4.3.4）：整数索引为虚拟 own 属性。 */
      JSValue *data = o->wrapperData();
      if (data != nullptr && data->kind() == Kind::String) {
        size_t len = utf16Length(data->toString());
        for (size_t i = 0; i < len; ++i) add_key(std::to_string(i));
      }
    }
    /* own 字符串键按创建序，过滤 enumerable。 */
    for (const std::string &name : o->propertyOrder()) {
      const PropertyDescriptor *d = o->getOwnProperty(name);
      if (d != nullptr && d->enumerable) add_key(name);
    }
  }
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

/* 一元按位非（§13.5.5）：~x。ToNumeric 后：
 * BigInt -> -x - 1n（§6.1.6.2.12 BigInt::bitwiseNOT）；
 * Number -> ToInt32 后按位取反。 */
ConversionResult unaryBitNot(JSContext *ctx, JSValue *a) {
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  JSValue *va = na.value;
  if (va->kind() == Kind::BigInt) {
    /* ~x = -x - 1n：先取 -x（翻转符号），再减 1（加 -1n）。 */
    bool neg = !va->bigIntNegative();
    std::string digits = va->bigIntDigits();
    bool outNeg = false;
    bigintAddSigned(neg, digits, true, "1", outNeg);
    if (digits == "0") outNeg = false;
    return ok(ctx->createValue(JSValue(outNeg, digits)));
  }
  int32_t x = toInt32Value(va->toNumber());
  return ok(ctx->createValue(int64_t(~x)));
}

/* 一元 ++/--（§13.4.4）：ToNumeric 后 ±1。
 * BigInt -> ±1n（任意精度）；Number -> IEEE +1/-1。 */
ConversionResult unaryIncrement(JSContext *ctx, JSValue *a, bool dec) {
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  JSValue *va = na.value;
  if (va->kind() == Kind::BigInt) {
    bool neg = va->bigIntNegative();
    std::string digits = va->bigIntDigits();
    bool outNeg = false;
    bigintAddSigned(neg, digits, dec, "1", outNeg);
    if (digits == "0") outNeg = false;
    return ok(ctx->createValue(JSValue(outNeg, digits)));
  }
  double d = va->toNumber();
  return ok(ctx->createValue(dec ? d - 1.0 : d + 1.0));
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

/* -------------------------------------------------------------------------- */
/* 分发表执行模型（M5.1e-f 栈帧瘦身）：handler 函数 + 全局静态分发表。        */
/* -------------------------------------------------------------------------- */


static ConversionResult op_PushConst(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      stack.push_back(constValue(frame.ctx, fn, frame.arg));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_PushBigInt(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* BigInt 常量（M4.3）：十进制数字串。 */
      stack.push_back(constValue(frame.ctx, fn, frame.arg));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_PushUndefined(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      stack.push_back(frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_PushNull(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      stack.push_back(frame.ctx->createValue(Kind::Null));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_PushTrue(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      stack.push_back(frame.ctx->createValue(true));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_PushFalse(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      stack.push_back(frame.ctx->createValue(false));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_PushInt(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      stack.push_back(frame.ctx->createValue(static_cast<int64_t>(
          static_cast<int16_t>(frame.arg))));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadVar(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (frame.arg >= stack.size()) {
        error = fail("ReferenceError: local slot out of range"); break;
      }
      /* M4.4：优先读环境绑定——内层闭包经 StoreUpvalue 更新外层局部后，
      * 外层 LoadVar 需读到最新值（顶层 var + forEach 回调场景）。
      * 环境无该绑定（如参数名未写入环境）时回退栈槽。 */
      if (frame.env != nullptr && frame.arg < fn.locals.size() &&
          !fn.locals[frame.arg].empty()) {
        const std::string &name = fn.locals[frame.arg];
        JSValue *bv = frame.env->getBinding(name);
        if (bv != nullptr) {
          stack.push_back(bv);
          break;
        }
      }
      stack.push_back(stack[frame.arg]);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StoreVar(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.empty() || frame.arg >= stack.size()) {
        error = fail("ReferenceError: local slot out of range"); break;
      }
      JSValue *val = stack.back();
      stack[frame.arg] = val; /* 栈顶 -> 局部槽 */
      /* M2.5：局部变量同步写入当前环境（内层闭包 LoadUpvalue 沿链查找）。 */
      if (frame.env != nullptr && frame.arg < fn.locals.size() &&
          !fn.locals[frame.arg].empty()) {
        frame.env->setBinding(fn.locals[frame.arg], val);
        /* M5.4 脚本顶层 var 双写全局对象（§13.3.2.4 GlobalDeclarationInstantiation
         * CreateGlobalVarBinding）：顶层函数声明（含 async）独立编译，体内读
         * 脚本顶层 var 走 LoadGlobal（读全局对象）；普通闭包 LoadUpvalue 读
         * top_env。在 op_StoreVar 汇聚点同步写全局对象，两条路径值一致。
         * 仅当当前帧 env 恰为脚本顶层 env（JS_Eval 顶层执行期间）时触发。 */
        if (frame.ctx->scriptTopEnv() != nullptr &&
            frame.env == frame.ctx->scriptTopEnv()) {
          JSValue *global = frame.ctx->globalObject();
          ConversionResult sr = setPropertyValue(
              frame.ctx, global->object(), fn.locals[frame.arg], val);
          if (!sr.ok()) return sr;
        }
      }
      stack.pop_back();       /* 弹出：赋值表达式不保留值在栈（链式赋值经 LoadVar） */
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadGlobal(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 全局对象属性读取：LoadGlobal {name_index} -> 属性值（未定义属性返回 undefined）。 */
      if (frame.arg >= fn.constants.size() ||
          fn.constants[frame.arg].is_number) {
        error = fail("Error: LoadGlobal requires a string constant"); break;
      }
      const std::string &name = fn.constants[frame.arg].str;
      JSValue *global = frame.ctx->globalObject();
      JSValue *val = global->object()->getProperty(name);
      stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StoreGlobal(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 隐式全局赋值（M5.1e-g）：栈顶值 -> 全局对象属性（创建/更新）。
      * 赋值表达式值 = 新值（保留栈顶）。非严格语义（§8.1.1.2.2 PutValue
      * 未声明标识符 -> globalThis 属性）。 */
      if (stack.empty()) {
        error = fail("Error: stack underflow on StoreGlobal"); break;
      }
      if (frame.arg >= fn.constants.size() ||
          fn.constants[frame.arg].is_number) {
        error = fail("Error: StoreGlobal requires a string constant"); break;
      }
      const std::string &name = fn.constants[frame.arg].str;
      JSValue *val = stack.back();
      JSValue *global = frame.ctx->globalObject();
      ConversionResult sr = setPropertyValue(frame.ctx, global->object(), name, val);
      if (!sr.ok()) return sr;
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadImport(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 模块导入绑定读取（M5.2a 选项 A 动态查表）：
       * LoadImport {import_index} -> 压入源模块导出绑定值。
       * fn.imports[arg] = {local_name, specifier, export_name}。
       * 运行期：ctx->findModule(specifier) 查源模块记录 -> findExport
       * 解出本地绑定名 -> 源模块 env getBinding 取当前值（live binding）。
       * 源模块未加载（循环依赖尚未完成）或导出不存在 -> undefined。 */
      if (frame.arg >= fn.imports.size()) {
        error = fail("Error: LoadImport index out of range"); break;
      }
      const ModuleImportEntry &entry = fn.imports[frame.arg];
      JSModuleRecord *from = frame.ctx->findModule(entry.specifier);
      if (from == nullptr || from->env() == nullptr) {
        stack.push_back(frame.ctx->createValue(Kind::Undefined));
        break;
      }
      /* 解析导出到最终模块 + 绑定名（M5.2b：findExport 内部完成 re-export /
       * export * 转发链解析，含环保护）。读最终模块 env 的 live binding。 */
      std::string local_name;
      JSModuleRecord *resolved = nullptr;
      if (!from->findExport(entry.export_name, &local_name, &resolved) ||
          resolved == nullptr || resolved->env() == nullptr) {
        stack.push_back(frame.ctx->createValue(Kind::Undefined));
        break;
      }
      JSValue *val = resolved->env()->getBinding(local_name);
      stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StoreImport(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 导入绑定赋值（M5.2a）：import 绑定是 immutable binding
       * （§16.2.3 SetMutableBinding 对 import 绑定抛 TypeError）。
       * 赋值表达式值 = 新值（保留栈顶，赋值语义）。 */
      if (frame.arg >= fn.imports.size()) {
        error = fail("Error: StoreImport index out of range"); break;
      }
      if (stack.empty()) {
        error = fail("Error: stack underflow on StoreImport"); break;
      }
      error = fail("TypeError: Assignment to constant variable"); break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadImportNamespace(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.2d 命名空间导入：import * as ns from 'm'（§10.4.6 模块 namespace
       * 镜像对象）。fn.imports[arg] 的 specifier 定位源模块 record，调用
       * createModuleNamespace 生成导出镜像（export_name -> live binding 值，
       * 含 default 与 re-export/export * 转发）。源模块未加载/无 env ->
       * undefined（与 LoadImport 缺省一致）。 */
      if (frame.arg >= fn.imports.size()) {
        error = fail("Error: LoadImportNamespace index out of range"); break;
      }
      const ModuleImportEntry &entry = fn.imports[frame.arg];
      JSModuleRecord *from = frame.ctx->findModule(entry.specifier);
      if (from == nullptr || from->env() == nullptr) {
        stack.push_back(frame.ctx->createValue(Kind::Undefined));
        break;
      }
      JSValue *ns = createModuleNamespace(frame.ctx, from);
      stack.push_back(ns != nullptr ? ns : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadImportMeta(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.2c import.meta：沿 env 链查找模块 env 预置的 "__import_meta__"
       * 绑定（模块求值开始时写入）。模块内嵌套函数（闭包）的 frame.env
       * parent 链最终指向模块 env，沿链可定位本模块的 import.meta 对象。
       * 非模块上下文（普通脚本，语法层已拦截）防御返回 undefined。 */
      JSValue *meta = nullptr;
      for (JSEnvironment *e = frame.env; e != nullptr; e = e->parent()) {
        JSValue *v = e->getBinding("__import_meta__");
        if (v != nullptr) { meta = v; break; }
      }
      stack.push_back(meta != nullptr ? meta
                                      : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Pop(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() <= fn.numLocals) {
        error = fail("Error: stack underflow on Pop"); break;
      }
      stack.pop_back();
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Dup(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 复制栈顶（M4.1 成员调用保留 this）。 */
      if (stack.size() <= fn.numLocals) {
        error = fail("Error: stack underflow on Dup"); break;
      }
      stack.push_back(stack.back());
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Add(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 操作数保持栈上（GC 根集保护）：add 内部分配触发 GC 也不会回收操作数。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on Add"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = add(frame.ctx, a, b);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Sub(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on arith"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = binaryArith(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Mul(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on arith"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = binaryArith(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Div(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on arith"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = binaryArith(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Mod(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on arith"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = binaryArith(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Pow(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on arith"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = binaryArith(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_BitAnd(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on bitwise"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = bitwiseOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_BitOr(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on bitwise"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = bitwiseOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_BitXor(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on bitwise"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = bitwiseOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LShift(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on bitwise"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = bitwiseOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_RShift(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on bitwise"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = bitwiseOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_URShift(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on bitwise"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = bitwiseOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Neg(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.empty()) { error = fail("Error: stack underflow on Neg"); break; }
      JSValue *a = stack.back(); /* 保持栈上，toNumeric 完成后再收缩 */
      ConversionResult n = toNumeric(frame.ctx, a);
      if (!n.ok()) return n;
      stack.pop_back();
      if (n.value->kind() == Kind::BigInt) {
      /* 一元取负（§13.5.4）：BigInt 翻转符号位，0n 仍为 0n。 */
        bool neg = !n.value->bigIntNegative();
        const std::string &digits = n.value->bigIntDigits();
        stack.push_back(frame.ctx->createValue(
            JSValue(neg && digits != "0", digits)));
      } else {
        stack.push_back(frame.ctx->createValue(-n.value->toNumber()));
      }
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Not(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.empty()) { error = fail("Error: stack underflow on Not"); break; }
      JSValue *a = stack.back();
      bool b = toBoolean(a);
      stack.pop_back();
      stack.push_back(frame.ctx->createValue(!b));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Typeof(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.empty()) { error = fail("Error: stack underflow on Typeof"); break; }
      JSValue *a = stack.back();
      ConversionResult r = typeOf(frame.ctx, a);
      if (!r.ok()) { error = std::move(r); break; }
      stack.pop_back();
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Eq(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on compare"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = compareOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StrictEq(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on compare"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = compareOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Lt(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on compare"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = compareOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Le(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on compare"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = compareOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Gt(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on compare"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = compareOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Ge(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on compare"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = compareOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Ne(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on compare"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = compareOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StrictNe(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.size() < 2) { error = fail("Error: stack underflow on compare"); break; }
      JSValue *b = stack[stack.size() - 1];
      JSValue *a = stack[stack.size() - 2];
      ConversionResult r = compareOp(frame.ctx, a, b, frame.op);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Jump(VmFrame &frame) {
  size_t &pc = *frame.pc;
  ConversionResult error;
  do {
      pc = frame.arg;
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_JumpIfFalse(VmFrame &frame) {
  auto &stack = *frame.stack;
  size_t &pc = *frame.pc;
  ConversionResult error;
  do {
      if (stack.empty()) { error = fail("Error: stack underflow on JumpIfFalse"); break; }
      JSValue *cond = stack.back(); stack.pop_back();
      if (!toBoolean(cond)) pc = frame.arg;
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_JumpIfTrue(VmFrame &frame) {
  auto &stack = *frame.stack;
  size_t &pc = *frame.pc;
  ConversionResult error;
  do {
      /* M4.4f：栈顶为 true 则跳转（for-of 的 done 判断）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on JumpIfTrue"); break; }
      JSValue *cond = stack.back(); stack.pop_back();
      if (toBoolean(cond)) pc = frame.arg;
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_JumpIfUndefined(VmFrame &frame) {
  auto &stack = *frame.stack;
  size_t &pc = *frame.pc;
  ConversionResult error;
  do {
      /* M5.1a：栈顶为 undefined 则跳转（解构默认值判断）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on JumpIfUndefined"); break; }
      JSValue *v = stack.back(); stack.pop_back();
      if (v->kind() == Kind::Undefined) pc = frame.arg;
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_JumpIfNullish(VmFrame &frame) {
  auto &stack = *frame.stack;
  size_t &pc = *frame.pc;
  ConversionResult error;
  do {
      /* M5.1b：栈顶为 null 或 undefined 则跳转（??= 短路判断）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on JumpIfNullish"); break; }
      JSValue *v = stack.back(); stack.pop_back();
      if (v->kind() == Kind::Null || v->kind() == Kind::Undefined) pc = frame.arg;
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Call(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      uint16_t argc = frame.arg;
      size_t func_slot = stack.size() - 1 - static_cast<size_t>(argc);
      if (func_slot >= stack.size()) {
        error = fail("Error: stack underflow on Call"); break;
      }
      /* 函数与参数保持在栈上（GC 根集保护），仅以索引引用：
      * call() 内部分配临时句柄可能触发 GC，提前弹出会被回收（悬垂）。 */
      JSValue *func = stack[func_slot];
      JSValue *const *call_args =
          argc > 0 ? &stack[func_slot + 1] : nullptr;

      /* 调用（E3 统一入口；this 用 undefined，非严格函数绑到 global）。 */
      JSValue *this_undef = frame.ctx->createValue(Kind::Undefined);
      ConversionResult r = call(frame.ctx, func, this_undef, call_args, argc);
      if (!r.ok()) { error = std::move(r); break; }
      /* 收缩：弹出函数 + argc 个参数，压入结果。
      * 返回值保护句柄（call Script 分支分配，防 GC 回收窗口期）：
      * 先 push 入栈（受根集保护）再释放句柄，顺序安全。 */
      stack.resize(stack.size() - 1 - static_cast<size_t>(argc));
      stack.push_back(r.value);
      if (r.tmp_handle != 0) frame.ctx->handles().free(r.tmp_handle);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_CallMethod(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 成员方法调用（M4.1）：栈布局 obj, func, args...（argc 个）。
      * this = obj（func_slot 下方一位）。 */
      uint16_t argc = frame.arg;
      size_t func_slot = stack.size() - 1 - static_cast<size_t>(argc);
      if (func_slot <= fn.numLocals) {
        error = fail("Error: stack underflow on CallMethod"); break;
      }
      JSValue *this_obj = stack[func_slot - 1];
      JSValue *func = stack[func_slot];
      JSValue *const *call_args =
          argc > 0 ? &stack[func_slot + 1] : nullptr;
      ConversionResult r = call(frame.ctx, func, this_obj, call_args, argc);
      if (!r.ok()) { error = std::move(r); break; }
      /* 收缩：弹出 this + func + argc 个参数，压入结果。 */
      stack.resize(stack.size() - 2 - static_cast<size_t>(argc));
      stack.push_back(r.value);
      if (r.tmp_handle != 0) frame.ctx->handles().free(r.tmp_handle);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Construct(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* new 构造（M4.2）：栈布局 ctor, args...（argc 个）。
      * this = 新对象（construct 内部创建）。 */
      uint16_t argc = frame.arg;
      size_t ctor_slot = stack.size() - 1 - static_cast<size_t>(argc);
      if (ctor_slot < fn.numLocals) {
        error = fail("Error: stack underflow on Construct"); break;
      }
      JSValue *ctor = stack[ctor_slot];
      JSValue *const *call_args =
          argc > 0 ? &stack[ctor_slot + 1] : nullptr;
      ConversionResult r = construct(frame.ctx, ctor, call_args, argc);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 1 - static_cast<size_t>(argc));
      stack.push_back(r.value);
      if (r.tmp_handle != 0) frame.ctx->handles().free(r.tmp_handle);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Ret(VmFrame &frame) {
  auto &stack = *frame.stack;
  JSValue *&frame_this = *frame.frame_this;
  ConversionResult error;
  do {
      /* M5.1d-c1 构造器返回语义（§10.2.2 Construct 步骤 12-14）：
      * return 对象 -> 用之；return 非对象或无 return -> 返回最终 this
      * （基类 = 构造器创建的实例；派生类 = super() 建立/替换的对象）。
      * 压栈保护 frame_this（栈是根集），避免替换后的对象无根可达被 GC。 */
      if (frame.self != nullptr && frame.self->isConstructor()) {
        if (stack.empty() || !stack.back()->isObject()) {
          stack.push_back(frame_this);
        }
      }
      if (stack.empty()) {
        /* return undefined：构造 return completion（thrown=undefined）。
         * try-finally 穿透检查后若无 finally 覆盖则直接完成。 */
        JSValue *undef = frame.ctx->createValue(Kind::Undefined);
        ConversionResult ret_comp;
        ret_comp.thrown = undef;
        size_t new_pc = 0;
        if (tryDispatch(frame.ctx, *frame.fn, *frame.pc, stack, ret_comp,
                        &new_pc, &frame.pending, &frame.has_pending,
                        &frame.pending_handle, frame.env,
                        /*is_return=*/true, &frame.pending_is_return)) {
          *frame.pc = new_pc;
          return ok(nullptr); /* 跳 finally，不置 done */
        }
        frame.done = true;
        return ok(undef);
      }
      JSValue *result = stack.back();
      /* M5.5b：return 穿透 try-finally（§13.10.5）。
       * return 在 try 区间内（有 finally 覆盖）时不直接完成，而是挂起
       * return completion（pending.thrown=result, pending_is_return=true）
       * + 跳 finally_start。finally 末尾 RethrowPending 感知 return completion
       * 时把 return value 压栈并置 done，runBytecode 据栈顶取返回值，实现
       * return 穿透 finally 语义。try 只有 catch 无 finally 时 return 不拦截。 */
      ConversionResult ret_comp;
      ret_comp.thrown = result;
      size_t new_pc = 0;
      if (tryDispatch(frame.ctx, *frame.fn, *frame.pc, stack, ret_comp,
                      &new_pc, &frame.pending, &frame.has_pending,
                      &frame.pending_handle, frame.env,
                      /*is_return=*/true, &frame.pending_is_return)) {
        *frame.pc = new_pc;
        return ok(nullptr); /* 跳 finally，不置 done */
      }
      frame.done = true; /* M5.1e-f：分发表主循环据此结束本帧 */
      return ok(result);
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Print(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      if (stack.empty()) { error = fail("Error: stack underflow on Print"); break; }
      JSValue *v = stack.back();
      /* toString 内部分配可能触发 GC：v 保持栈上受根集保护。 */
      if (v->kind() == Kind::String) {
        std::fputs(v->toString().c_str(), stdout);
      } else {
        ConversionResult s = toString(frame.ctx, v);
        if (!s.ok()) return s;
        std::fputs(s.value->toString().c_str(), stdout);
      }
      std::fputs("\n", stdout);
      stack.pop_back();
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_MakeFunction(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  JSValue *&frame_this = *frame.frame_this;
  ConversionResult error;
  do {
      /* 从嵌套函数表实例化 JSFunction，[[Environment]] 绑定当前环境（M2.5）。
      * 嵌套函数字节码（fn.nested[arg]）复制给新函数。 */
      if (frame.arg >= fn.nested.size()) {
        error = fail("Error: MakeFunction index out of range"); break;
      }
      /* 函数原型链 -> Function.prototype（M4.2）：Script 函数挂 call/apply/
      * bind 方法（§20.2.3）。惰性创建缓存，未注册内置时原型为 null。 */
      JSObject *fproto = nullptr;
      JSValue *fp = neojs::functionProto(frame.ctx);
      if (fp != nullptr) fproto = fp->object();
      JSFunction *f = frame.ctx->heap().allocGCObject<JSFunction>(
          fproto, FunctionKind::Script, nullptr /* func */);
      const FunctionBytecode &nf = fn.nested[frame.arg];
      f->setBytecode(nf); /* 复制（FunctionBytecode 值语义） */
      f->setEnvironment(frame.env);
      /* M5.1d-a class：构造器/方法标志 -> 构造器可 new + 严格模式。
      * class 体一律严格模式（§15.7.2.1）；构造器额外 setConstructor。 */
      if (nf.is_constructor) {
        f->setConstructor(true);
        f->setThisMode(ThisMode::Strict);
      } else if (nf.is_method) {
        f->setThisMode(ThisMode::Strict);
      }
      /* M5.1d-b 箭头函数：super 上下文继承（§15.2.3.7 步骤 6——箭头
      * 函数没有自己的 [[HomeObject]]，super 解析到外层方法）。
      * 外层帧 self 是方法（有 HomeObject）时复制给箭头函数。 */
      if (nf.is_arrow && frame.self != nullptr && frame.self->homeObject() != nullptr) {
        f->setHomeObject(frame.self->homeObject());
      }
      /* M5.1d-c2 箭头函数词法 this（§10.2.5.6 Lexical）：箭头函数
      * [[ThisMode]] = Lexical，调用时忽略传入 this，用定义处外层代码
      * 的 this（当前帧 this）。捕获后由 gcMarkRefs 作为引用边保护。 */
      if (nf.is_arrow) {
        f->setThisMode(ThisMode::Lexical);
        f->setLexicalThis(frame_this);
      }
      frame.ctx->heap().pin(f); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
      JSValue *v = frame.ctx->heap().allocGCObject<JSValue>();
      v->setObject(f);
      f->setHolder(v);
      frame.ctx->heap().pin(v); /* v 未入栈前防误回收（GC 窗口，含 prototype 创建） */
      /* §20.2.3 步骤 8-10：普通函数创建 prototype 属性（箭头函数除外）。
      * prototype = 新对象（原型链 Object.prototype），含 constructor
      * 反向指针。class 构造器 prototype 由编译器 define_prop 显式覆盖
      * （compiler.cc compileClass 步骤 3），此处不设。 */
      if (!nf.is_arrow && !nf.is_constructor && !nf.is_method) {
        JSObject *op = objectProto(frame.ctx) ? objectProto(frame.ctx)->object() : nullptr;
        JSValue *proto = frame.ctx->createObject(op);
        frame.ctx->heap().pin(proto);
        {
      /* constructor 反向指针：non-enumerable（§20.2.3 步骤 8 的
      * CreateDataProperty 实为 non-enumerable，规范步骤 9）。 */
          PropertyDescriptor cd;
          cd.value = v;
          cd.enumerable = false;
          proto->object()->setOwnProperty("constructor", cd);
        }
        PropertyDescriptor pd;
        pd.value = proto;
        pd.enumerable = false; /* 规范：prototype 属性 non-enumerable */
        f->setOwnProperty("prototype", pd);
        frame.ctx->heap().unpin(proto);
      }
      frame.ctx->heap().unpin(v);
      frame.ctx->heap().unpin(f);
      stack.push_back(v);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_NewObject(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 新建普通对象，原型链指向 Object.prototype（M3.3；
      * 惰性创建缓存，未注册内置时原型为 null）。 */
      JSValue *obj =
          frame.ctx->createObject(objectProto(frame.ctx) ? objectProto(frame.ctx)->object()
                                             : nullptr);
      stack.push_back(obj);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_NewArray(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 新建数组（空 elements，length = 0）。 */
      JSValue *arr = frame.ctx->createArray(0);
      stack.push_back(arr);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadProp(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* obj.key -> 栈顶（替换 obj）。key 为常量池字符串。 */
      if (stack.empty()) { error = fail("Error: stack underflow on LoadProp"); break; }
      JSValue *obj = stack.back();
      if (frame.arg >= fn.constants.size() ||
          fn.constants[frame.arg].is_number) {
        error = fail("Error: LoadProp requires a string constant"); break;
      }
      const std::string &key = fn.constants[frame.arg].str;
      /* 原始值成员读取（§13.3.3 + §7.1.13）：先 ToObject 装箱查属性，
      * 方法调用时 this 仍为原始值（getValue 返回原始值）。undefined/null
      * 装箱失败 -> TypeError。 */
      JSObject *target = nullptr;
      if (obj->isObject()) {
        target = obj->object();
      } else if (obj->kind() == Kind::Undefined ||
                 obj->kind() == Kind::Null) {
        error = fail("TypeError: Cannot read properties of " +
                    (obj->kind() == Kind::Undefined ? std::string("undefined")
                                                    : std::string("null"))); break;
      } else {
        ConversionResult boxed = toObject(frame.ctx, obj);
        if (!boxed.ok()) return boxed;
        target = boxed.value->object();
      }
      JSValue *val = nullptr;
      if (target->isArray() && key == "length") {
      /* 数组 length 属性（M4.1）：由 elements 存储推导，非存储属性。 */
        val = frame.ctx->createValue(static_cast<double>(target->length()));
      } else if (target->isMap() && key == "size") {
      /* Map size 属性（M4.4b，§24.1.3.13）：由条目数推导，非存储属性。 */
        val = frame.ctx->createValue(static_cast<double>(target->collectionSize()));
      } else if (target->isSet() && key == "size") {
      /* Set size 属性（M4.4c，§24.2.3.9）：由条目数推导，非存储属性。 */
        val = frame.ctx->createValue(static_cast<double>(target->collectionSize()));
      } else if (target->isWrapper() &&
                 target->kind() == ObjectKind::String &&
                 (key == "length" || isCanonicalArrayIndex(key))) {
      /* String 包装对象/原始串的虚拟索引属性（§10.4.3.3）：
      * length 由 [[StringData]] 的 UTF-16 code unit 数推导；
      * 整数索引 key（如 "0"）返回对应 code unit 字符（'abc'[1] == 'b'）。 */
        JSValue *data = target->wrapperData();
        if (data != nullptr && data->kind() == Kind::String) {
          const std::string &s = data->toString();
          if (key == "length") {
            val = frame.ctx->createValue(static_cast<double>(utf16Length(s)));
          } else {
            size_t idx = static_cast<size_t>(std::stoul(key));
            size_t len = utf16Length(s);
            if (idx < len) {
              val = frame.ctx->createValue(s.substr(idx, 1));
            } else {
              val = frame.ctx->createValue(Kind::Undefined);
            }
          }
        } else {
          val = frame.ctx->createValue(Kind::Undefined);
        }
      } else {
      /* M4.4h：访问器感知读取（数据属性直接取值；访问器调 getter）。 */
        ConversionResult gv = getPropertyValue(frame.ctx, target, key);
        if (!gv.ok()) return gv;
        val = gv.value;
      }
      stack.pop_back();
      stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StoreProp(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* obj, val -> 栈顶 val（obj.key = val）。key 为常量池字符串。
      * 消费 obj（编译器成员赋值后不再需要对象引用）。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on StoreProp"); break; }
      JSValue *val = stack.back();
      JSValue *obj = stack[stack.size() - 2];
      if (frame.arg >= fn.constants.size() ||
          fn.constants[frame.arg].is_number) {
        error = fail("Error: StoreProp requires a string constant"); break;
      }
      const std::string &key = fn.constants[frame.arg].str;
      if (!obj->isObject()) {
        error = fail("TypeError: Cannot set properties of non-object"); break;
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
          error = fail("TypeError: Cannot assign to read only property 'length'"); break;
        }
        double d = val->toNumber();
        if (d < 0.0 || d != std::trunc(d) || d > 4294967295.0) {
          error = fail("RangeError: Invalid array length"); break;
        }
        obj->object()->resizeElements(static_cast<size_t>(d));
      } else {
      /* M4.4h：访问器感知写入（writable 校验 + setter 调用）。 */
        ConversionResult sr = setPropertyValue(frame.ctx, obj->object(), key, val);
        if (!sr.ok()) return sr;
      }
      stack.resize(stack.size() - 2);
      stack.push_back(val); /* 赋值表达式值 = 新值 */
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadElem(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* obj, key -> 栈顶（替换 obj）。key 运行期 ToPropertyKey。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on LoadElem"); break; }
      JSValue *key = stack.back();
      JSValue *obj = stack[stack.size() - 2];
      /* 数组整数索引快速路径（架构 §5.2：arr[i] -> elements）。 */
      if (obj->isObject() && obj->object()->isArray() && key->isNumber()) {
        double d = key->toNumber();
        if (d >= 0.0 && d == std::trunc(d) && d < 4294967296.0) {
          JSValue *val = obj->object()->getElement(static_cast<size_t>(d));
          stack.resize(stack.size() - 2);
          stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
          break;
        }
      }
      ConversionResult pk = toPropertyKey(frame.ctx, key);
      if (!pk.ok()) return pk;
      /* Symbol 属性键（M4.4d）：查 Symbol 属性表（沿原型链）。
      * 原始值（如 'ab'[Symbol.iterator]）先 ToObject 装箱（§13.3.3
      * Property Accessors 的 RequireObjectCoercible + ToObject）。 */
      if (pk.value->kind() == Kind::Symbol) {
        JSValue *symbol_target = obj;
        if (!obj->isObject()) {
          if (obj->kind() == Kind::Undefined || obj->kind() == Kind::Null) {
            error = fail("TypeError: Cannot read properties of " +
                        (obj->kind() == Kind::Undefined
                             ? std::string("undefined")
                             : std::string("null"))); break;
          }
          ConversionResult boxed = toObject(frame.ctx, obj);
          if (!boxed.ok()) return boxed;
          symbol_target = boxed.value;
        }
        JSValue *val = symbol_target->object()->getSymbolProperty(pk.value);
        stack.resize(stack.size() - 2);
        stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
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
            JSValue *val = frame.ctx->createValue(
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
        error = fail("TypeError: Cannot read properties of " +
                    (obj->kind() == Kind::Undefined ? std::string("undefined")
                                                    : std::string("null"))); break;
      } else {
        ConversionResult boxed = toObject(frame.ctx, obj);
        if (!boxed.ok()) return boxed;
        target = boxed.value->object();
      }
      /* M4.4h：访问器感知读取（数据属性直接取值；访问器调 getter）。 */
      ConversionResult gv = getPropertyValue(frame.ctx, target, pk.value->toString());
      if (!gv.ok()) return gv;
      JSValue *val = gv.value;
      stack.resize(stack.size() - 2);
      stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StoreElem(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* obj, key, val -> 栈顶 val（obj[key] = val）。 */
      if (stack.size() < 3) { error = fail("Error: stack underflow on StoreElem"); break; }
      JSValue *val = stack.back();
      JSValue *key = stack[stack.size() - 2];
      JSValue *obj = stack[stack.size() - 3];
      if (!obj->isObject()) {
        error = fail("TypeError: Cannot set properties of non-object"); break;
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
      ConversionResult pk = toPropertyKey(frame.ctx, key);
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
          setPropertyValue(frame.ctx, obj->object(), pk.value->toString(), val);
      if (!sr.ok()) return sr;
      stack.resize(stack.size() - 3);
      stack.push_back(val); /* 赋值表达式值 = 新值 */
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_GetIterator(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 栈顶对象 -> 迭代器（M4.4f，§7.4.1）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on GetIterator"); break; }
      ConversionResult it = getIterator(frame.ctx, stack.back());
      if (!it.ok()) return it;
      stack.back() = it.value; /* 替换栈顶为迭代器 */
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_IteratorStep(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 栈顶 iterator -> [value, done]（done 在栈顶，M4.4f，§7.4.5）。
      * 完成时 value 为 undefined（规范 IteratorValue 未定义值）。
      * iteratorStep 完成返回 ok(nullptr)（成功但无值，§7.4.5 done=true），
      * 与失败（value=nullptr + error 非空）区分：仅 error 非空才真失败。 */
      if (stack.empty()) { error = fail("Error: stack underflow on IteratorStep"); break; }
      ConversionResult step = iteratorStep(frame.ctx, stack.back());
      if (step.value == nullptr && !step.error.empty()) return step;
      if (step.value == nullptr) {
      /* 迭代完成：压 [undefined, true]。 */
        stack.back() = frame.ctx->createValue(Kind::Undefined);
        stack.push_back(frame.ctx->createValue(true));
      } else {
        ConversionResult v = iteratorValue(frame.ctx, step.value);
        if (!v.ok()) return v;
        stack.back() = v.value; /* value */
        stack.push_back(frame.ctx->createValue(false)); /* done=false */
      }
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* M5.5b IteratorClose（§7.4.6）：栈顶 iterator 弹栈，按帧 pending 状态
 * 构造 completion 调 iteratorClose。
 *   has_pending -> abrupt completion（pending 作 completion），清 pending；
 *   否则 -> normal completion（占位 undefined）。
 * iteratorClose 内部调 iterator.return()（若有），返回非对象则 TypeError。
 * 结果：正常时压 undefined 到栈（保持栈平衡，调用方约定 close 不产生值）；
 * iteratorClose 返回 abrupt -> 直接抛出（异常传播，覆盖原 completion）。 */
static ConversionResult op_IteratorClose(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    if (stack.empty()) {
      error = fail("Error: stack underflow on IteratorClose");
      break;
    }
    JSValue *iterator = stack.back();
    stack.pop_back();

    /* 构造 completion：有挂起异常 -> abrupt；否则 normal（占位 undefined）。 */
    ConversionResult completion;
    if (frame.has_pending) {
      completion = std::move(frame.pending);
      frame.has_pending = false;
      if (frame.pending_handle != 0) {
        frame.ctx->handles().free(frame.pending_handle);
        frame.pending_handle = 0;
      }
    } else {
      completion.value = frame.ctx->createValue(Kind::Undefined);
    }

    ConversionResult r = iteratorClose(frame.ctx, iterator, std::move(completion));
    /* iteratorClose 返回的 completion：若 abrupt 则抛出（异常传播）；
     * normal 则压 undefined 占位（保持栈平衡）。 */
    if (r.thrown != nullptr || !r.error.empty()) {
      /* close 抛错：作为新异常传播（覆盖原 completion，§7.4.6 step 3.c）。 */
      error = std::move(r);
      break;
    }
    stack.push_back(frame.ctx->createValue(Kind::Undefined));
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* M5.5c-2 GetAsyncIterator（§14.7.5.1）：栈顶 iterable -> async iterator。
 *   - 优先 iterable[Symbol.asyncIterator]()；无则回退同步
 *     iterable[Symbol.iterator]() 并包装成 AsyncFromSyncIterator。
 * 替换栈顶为 async iterator（不挂起）。 */
static ConversionResult op_GetAsyncIterator(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    if (stack.empty()) {
      error = fail("Error: stack underflow on GetAsyncIterator");
      break;
    }
    ConversionResult it = getAsyncIterator(frame.ctx, stack.back());
    if (!it.ok()) return it;
    stack.back() = it.value; /* 替换栈顶为 async iterator */
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* M5.5c-2 AsyncIteratorNext（§14.7.5.3）：栈顶 async iterator -> 调
 * next() + PromiseResolve 接合，结果 promise 替换栈顶。
 * 不挂起——后续 Await 指令统一挂起/恢复（await 挂起机制复用）。
 * 注：不弹栈——抽象运算内部 createValue/createDerivedPromise 分配可能触发
 * GC，iterator 需保持在栈上受根集保护（对齐同步 op_IteratorStep 的
 * stack.back() 替换模式，防悬垂）。 */
static ConversionResult op_AsyncIteratorNext(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    if (stack.empty()) {
      error = fail("Error: stack underflow on AsyncIteratorNext");
      break;
    }
    JSValue *iterator = stack.back();
    ConversionResult r = asyncIteratorNext(frame.ctx, iterator);
    if (!r.ok()) return r;
    stack.back() = r.value; /* 结果 promise 替换栈顶（后续 Await 挂起） */
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* M5.5c-2 AsyncIteratorClose（§14.7.5.4）：栈顶 async iterator -> 调
 * return()（若有）+ PromiseResolve 接合，结果 promise 替换栈顶。
 * 不挂起——后续 Await 指令统一挂起/恢复。
 * 注：不弹栈（同上，防 GC 悬垂）。本 v1 实现不额外校验 return 结果对象性
 * （规范要求对象时抛 TypeError），由调用方经 Await 恢复后按 completion 处理。 */
static ConversionResult op_AsyncIteratorClose(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    if (stack.empty()) {
      error = fail("Error: stack underflow on AsyncIteratorClose");
      break;
    }
    JSValue *iterator = stack.back();
    ConversionResult r = asyncIteratorClose(frame.ctx, iterator);
    if (!r.ok()) return r;
    stack.back() = r.value; /* 结果 promise 替换栈顶（后续 Await 挂起） */
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* M5.5c-2 IteratorResultToValueDone：栈顶 {value, done} 对象 -> 弹栈，
 * 压 value + done（done 在栈顶，对齐同步 IteratorStep 布局）。
 * AsyncIteratorNext + Await 恢复后栈顶是异步迭代结果对象，转二元组供
 * JumpIfTrue（done 判断）/ left 赋值（value）使用。
 * 注：done 读为 ToBoolean（§14.7.5.3 step 5.d IteratorComplete），value
 * 直接取属性值（§14.7.5.3 step 5.b IteratorValue 简化——v1 不校验对象性）。 */
static ConversionResult op_IteratorResultToValueDone(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    if (stack.empty()) {
      error = fail("Error: stack underflow on IteratorResultToValueDone");
      break;
    }
    JSValue *result = stack.back();
    stack.pop_back();
    if (result == nullptr || !result->isObject()) {
      error = fail("TypeError: Iterator result is not an object");
      break;
    }
    JSObject *ro = result->object();
    JSValue *val = ro->getProperty("value");
    JSValue *done = ro->getProperty("done");
    stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
    stack.push_back(frame.ctx->createValue(done != nullptr && toBoolean(done)));
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_RestToArray(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1a rest/展开：栈 [iterable, start] -> 剩余元素数组。
      * 迭代 iterable，跳过前 start 个元素，其余收集为新数组。
      * start 必须为非负整数（编译期保证）。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on RestToArray"); break; }
      JSValue *start_v = stack.back();
      JSValue *iterable = stack[stack.size() - 2];
      size_t start = start_v->isNumber()
                         ? static_cast<size_t>(std::max(0.0, start_v->toNumber()))
                         : 0;
      ConversionResult it = getIterator(frame.ctx, iterable);
      if (!it.ok()) return it;
      JSValue *iterator = it.value;
      /* 跳过前 start 个。 */
      size_t skipped = 0;
      while (skipped < start) {
        ConversionResult step = iteratorStep(frame.ctx, iterator);
        if (step.value == nullptr && !step.error.empty()) return step;
        if (step.value == nullptr) break; /* 迭代提前结束 */
        ++skipped;
      }
      /* 收集剩余元素。 */
      JSValue *arr = frame.ctx->createArray(0);
      size_t idx = 0;
      for (;;) {
        ConversionResult step = iteratorStep(frame.ctx, iterator);
        if (step.value == nullptr && !step.error.empty()) return step;
        if (step.value == nullptr) break; /* 迭代完成 */
        ConversionResult v = iteratorValue(frame.ctx, step.value);
        if (!v.ok()) return v;
        arr->object()->setElement(idx++, v.value);
      }
      stack.resize(stack.size() - 2);
      stack.push_back(arr);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_RestParam(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1a rest 参数：args[k..] 收集为新数组压栈。
      * runBytecode 入口参数（args/argc）在本帧仍有效（调用栈递归
      * 每帧独立）。数组经 createArray 创建后立即入栈（根集保护）。 */
      size_t k = frame.arg;
      JSValue *arr = frame.ctx->createArray(0);
      size_t idx = 0;
      for (size_t i = k; i < frame.argc; ++i) {
        if (frame.args[i] != nullptr) arr->object()->setElement(idx++, frame.args[i]);
      }
      stack.push_back(arr);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_AppendElem(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1a 数组展开：弹栈值追加到槽内数组末尾（动态索引）。 */
      if (stack.empty() || frame.arg >= fn.numLocals) {
        error = fail("Error: stack underflow on AppendElem"); break;
      }
      JSValue *val = stack.back();
      stack.pop_back();
      JSValue *arr = stack[frame.arg];
      if (arr == nullptr || !arr->isObject() || !arr->object()->isArray()) {
        error = fail("TypeError: AppendElem target is not an array"); break;
      }
      size_t len = arr->object()->length();
      arr->object()->setElement(len, val);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_AppendSpread(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1a 数组展开：弹栈可迭代对象，其元素按序追加到槽内数组。
      * 迭代器入栈保护（循环期间 getIterator/iteratorStep/iteratorValue
      * 可能分配触发 GC，迭代器不在栈上会悬垂）。 */
      if (stack.empty() || frame.arg >= fn.numLocals) {
        error = fail("Error: stack underflow on AppendSpread"); break;
      }
      JSValue *iterable = stack.back();
      stack.pop_back();
      JSValue *arr = stack[frame.arg];
      if (arr == nullptr || !arr->isObject() || !arr->object()->isArray()) {
        error = fail("TypeError: AppendSpread target is not an array"); break;
      }
      ConversionResult it = getIterator(frame.ctx, iterable);
      if (!it.ok()) return it;
      JSValue *iterator = it.value;
      stack.push_back(iterator); /* 迭代器入栈根集保护（循环期间） */
      for (;;) {
        ConversionResult step = iteratorStep(frame.ctx, iterator);
        if (step.value == nullptr && !step.error.empty()) {
          stack.pop_back();
          return step;
        }
        if (step.value == nullptr) break; /* 迭代完成 */
        ConversionResult v = iteratorValue(frame.ctx, step.value);
        if (!v.ok()) {
          stack.pop_back();
          return v;
        }
        size_t len = arr->object()->length();
        arr->object()->setElement(len, v.value);
      }
      stack.pop_back(); /* 弹出迭代器保护 */
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_CallArray(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1a 展开调用：栈 [func, args_array] -> 调用结果。
      * args_array 元素（可能为 null=空洞）按序为参数。
      * func 与 args_array 保持在栈（GC 根集），flatten 到临时
      * vector 后调用（call 内部分配触发 GC，vector 内指针源自栈）。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on CallArray"); break; }
      JSValue *args_arr = stack.back();
      JSValue *func = stack[stack.size() - 2];
      if (args_arr == nullptr || !args_arr->isObject() ||
          !args_arr->object()->isArray()) {
        error = fail("TypeError: spread argument must be iterable"); break;
      }
      JSObject *aobj = args_arr->object();
      std::vector<JSValue *> flat;
      flat.reserve(aobj->length());
      for (size_t i = 0; i < aobj->length(); ++i) {
        JSValue *e = aobj->getElement(i);
        flat.push_back(e != nullptr ? e : frame.ctx->createValue(Kind::Undefined));
      }
      JSValue *this_undef = frame.ctx->createValue(Kind::Undefined);
      ConversionResult r =
          call(frame.ctx, func, this_undef,
               flat.empty() ? nullptr : flat.data(), flat.size());
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      if (r.tmp_handle != 0) frame.ctx->handles().free(r.tmp_handle);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_CallMethodArray(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e-h 成员展开调用：栈 [obj, func, args_array] -> 调用结果。
      * this = obj（args_array 下方两位）。args_array 元素按序为参数。 */
      if (stack.size() < 3) { error = fail("Error: stack underflow on CallMethodArray"); break; }
      JSValue *args_arr = stack.back();
      JSValue *func = stack[stack.size() - 2];
      JSValue *this_obj = stack[stack.size() - 3];
      if (args_arr == nullptr || !args_arr->isObject() ||
          !args_arr->object()->isArray()) {
        error = fail("TypeError: spread argument must be iterable"); break;
      }
      JSObject *aobj = args_arr->object();
      std::vector<JSValue *> flat;
      flat.reserve(aobj->length());
      for (size_t i = 0; i < aobj->length(); ++i) {
        JSValue *e = aobj->getElement(i);
        flat.push_back(e != nullptr ? e : frame.ctx->createValue(Kind::Undefined));
      }
      ConversionResult r =
          call(frame.ctx, func, this_obj,
               flat.empty() ? nullptr : flat.data(), flat.size());
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 3);
      stack.push_back(r.value);
      if (r.tmp_handle != 0) frame.ctx->handles().free(r.tmp_handle);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_ConstructArray(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1a 展开 new：栈 [ctor, args_array] -> 构造实例。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on ConstructArray"); break; }
      JSValue *args_arr = stack.back();
      JSValue *ctor = stack[stack.size() - 2];
      if (args_arr == nullptr || !args_arr->isObject() ||
          !args_arr->object()->isArray()) {
        error = fail("TypeError: spread argument must be iterable"); break;
      }
      JSObject *aobj = args_arr->object();
      std::vector<JSValue *> flat;
      flat.reserve(aobj->length());
      for (size_t i = 0; i < aobj->length(); ++i) {
        JSValue *e = aobj->getElement(i);
        flat.push_back(e != nullptr ? e : frame.ctx->createValue(Kind::Undefined));
      }
      ConversionResult r =
          construct(frame.ctx, ctor, flat.empty() ? nullptr : flat.data(), flat.size());
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      if (r.tmp_handle != 0) frame.ctx->handles().free(r.tmp_handle);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_CopyObjectProps(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1a 对象展开 {…src}：弹栈源对象，own enumerable 字符串属性
      * 复制进槽内目标对象（CopyDataProperties，§10.1.2.1）。
      * 数组源：整数索引元素为 own enumerable 键。
      * 原始值源：ToObject 装箱后取属性（null/undefined 跳过）。 */
      if (stack.empty() || frame.arg >= fn.numLocals) {
        error = fail("Error: stack underflow on CopyObjectProps"); break;
      }
      JSValue *src = stack.back();
      stack.pop_back();
      JSValue *target = stack[frame.arg];
      if (target == nullptr || !target->isObject()) {
        error = fail("TypeError: object spread target is not an object"); break;
      }
      if (src == nullptr || src->kind() == Kind::Undefined ||
          src->kind() == Kind::Null) {
        break; /* null/undefined 源跳过（CopyDataProperties 步骤 2a） */
      }
      ConversionResult boxed = toObject(frame.ctx, src);
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
              v = frame.ctx->createValue(Kind::Undefined);
            } else {
              ConversionResult r = call(frame.ctx, d->getter, sobj->holder(), nullptr, 0);
              if (!r.ok()) { error = std::move(r); break; }
              v = r.value;
            }
          } else {
            v = d != nullptr && d->value != nullptr ? d->value
                                                    : frame.ctx->createValue(Kind::Undefined);
          }
        }
        if (v == nullptr) v = frame.ctx->createValue(Kind::Undefined);
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
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_DefineProp(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1c 计算键 { [expr]: value }：obj, key, val -> 栈顶 val。
      * 定义 own 数据属性（key 运行期 ToPropertyKey，§7.3.14）。 */
      if (stack.size() < 3) { error = fail("Error: stack underflow on DefineProp"); break; }
      JSValue *val = stack.back();
      JSValue *key = stack[stack.size() - 2];
      JSValue *obj = stack[stack.size() - 3];
      if (!obj->isObject()) {
        error = fail("TypeError: Cannot define properties on non-object"); break;
      }
      ConversionResult pk = toPropertyKey(frame.ctx, key);
      if (!pk.ok()) return pk;
      JSObject *target = obj->object();
      if (pk.value->kind() == Kind::Symbol) {
        /* Symbol 计算键（M5.5c-2）：存 Symbol 属性表（§7.3.14）。 */
        PropertyDescriptor d;
        d.value = val;
        target->setOwnSymbolProperty(pk.value, d);
      } else {
        const std::string &name = pk.value->toString();
        bool numeric = !name.empty() &&
                       name.find_first_not_of("0123456789") == std::string::npos;
        if (target->isArray() && numeric && name.size() <= 18) {
        /* 数组整数索引计算键：写元素存储。 */
          target->setElement(static_cast<size_t>(std::strtoull(name.c_str(), nullptr, 10)), val);
        } else {
          PropertyDescriptor d;
          d.value = val;
          target->setOwnProperty(name, d);
        }
      }
      stack.resize(stack.size() - 3);
      stack.push_back(val);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_DefineAccessor(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1c getter/setter { get key(){} } / { set key(v){} }：
      * obj, key, fn -> 栈顶 fn。arg=0 定义 getter，arg=1 定义 setter。
      * 访问器属性描述符：getter/setter 槽，Type::Accessor。 */
      if (stack.size() < 3) { error = fail("Error: stack underflow on DefineAccessor"); break; }
      JSValue *fn = stack.back();
      JSValue *key = stack[stack.size() - 2];
      JSValue *obj = stack[stack.size() - 3];
      if (!obj->isObject()) {
        error = fail("TypeError: Cannot define properties on non-object"); break;
      }
      ConversionResult pk = toPropertyKey(frame.ctx, key);
      if (!pk.ok()) return pk;
      JSObject *target = obj->object();
      const bool is_setter = frame.arg != 0;
      if (pk.value->kind() == Kind::Symbol) {
        /* Symbol 计算键访问器：存 Symbol 属性表（§7.3.14）。 */
        PropertyDescriptor *desc = target->getOwnSymbolProperty(pk.value);
        if (desc == nullptr) {
          PropertyDescriptor d;
          d.type = PropertyDescriptor::Type::Accessor;
          d.writable = false;
          target->setOwnSymbolProperty(pk.value, d);
          desc = target->getOwnSymbolProperty(pk.value);
        } else {
          desc->type = PropertyDescriptor::Type::Accessor;
          desc->writable = false;
        }
        if (is_setter) {
          desc->setter = fn;
        } else {
          desc->getter = fn;
        }
      } else {
        const std::string &name = pk.value->toString();
        PropertyDescriptor *desc = target->getOwnProperty(name);
        if (desc == nullptr) {
        /* 新建访问器描述符。 */
          PropertyDescriptor d;
          d.type = PropertyDescriptor::Type::Accessor;
          d.writable = false; /* 访问器属性无 writable 语义 */
          target->setOwnProperty(name, d);
          desc = target->getOwnProperty(name);
        } else {
        /* 已存在：升级为访问器类型（覆盖数据属性/互补 getter/setter）。 */
          desc->type = PropertyDescriptor::Type::Accessor;
          desc->writable = false;
        }
        if (is_setter) {
          desc->setter = fn;
        } else {
          desc->getter = fn;
        }
      }
      stack.resize(stack.size() - 3);
      stack.push_back(fn);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadThis(VmFrame &frame) {
  auto &stack = *frame.stack;
  JSValue *&frame_this = *frame.frame_this;
  ConversionResult error;
  do {
      /* M5.1c：当前帧 this -> 栈顶。frame_this 已由 runBytecode 解析
      * （函数调用 = call 传入的 thisArg；顶层 = globalThis）。 */
      stack.push_back(frame_this != nullptr ? frame_this
                                            : frame.ctx->globalObject());
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_SetPrototype(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1d-a class extends：obj, proto -> 设置 obj.[[Prototype]]=proto，
      * 弹 2 压回 obj。class 的 prototype 与构造器均非可扩展限制目标
      * （普通对象），直接 setPrototype。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on SetPrototype"); break; }
      JSValue *proto = stack.back();
      JSValue *obj = stack[stack.size() - 2];
      if (!obj->isObject()) {
        error = fail("TypeError: Cannot set prototype of non-object"); break;
      }
      JSObject *target = obj->object();
      JSObject *new_proto = (proto != nullptr && proto->isObject())
                                ? proto->object()
                                : nullptr;
      /* 简易防循环（class extends 场景原型链通常无环）：仅校验直接赋值。 */
      target->setPrototype(new_proto);
      stack.resize(stack.size() - 2);
      stack.push_back(obj);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_SetHomeObject(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1d-b class super：设置 fn.[[HomeObject]] = obj。
      * arg=0：obj, key, fn -> obj, key, fn（方法定义，原地不改栈）；
      * arg=1：obj, fn -> fn（构造器/static 方法，obj 在 size-2）。
      * HomeObject = 方法定义时所在对象：实例方法 = prototype，
      * static/构造器 = 构造器对象自身。 */
      const bool self_form = frame.arg != 0;
      size_t obj_pos = self_form ? stack.size() - 2 : stack.size() - 3;
      if (stack.size() < (self_form ? 2u : 3u)) {
        error = fail("Error: stack underflow on SetHomeObject"); break;
      }
      JSValue *fn = stack.back();
      JSValue *obj = stack[obj_pos];
      if (!fn->isObject()) {
        error = fail("TypeError: method is not an object"); break;
      }
      JSObject *home = (obj != nullptr && obj->isObject()) ? obj->object()
                                                           : nullptr;
      /* 仅 JSFunction 需要 HomeObject；非函数对象（未来字段值）忽略。 */
      JSFunction *method = dynamic_cast<JSFunction *>(fn->object());
      if (method != nullptr) method->setHomeObject(home);
      if (self_form) {
      /* obj, fn -> fn：obj 槽覆盖为 fn，去重末尾副本。 */
        stack[stack.size() - 2] = fn;
        stack.pop_back();
      }
      /* arg=0：栈保持 [obj, key, fn] 不变。 */
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_SuperLoadProp(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  JSValue *&frame_this = *frame.frame_this;
  ConversionResult error;
  do {
      /* M5.1d-b super.key：从 HomeObject.[[Prototype]] 读取 key。
      * 访问器属性 this = 当前帧 this（§12.3.5.3 MakeSuperPropertyReference）。
      * HomeObject 缺失（super 编译期已校验上下文，运行期防御）-> undefined。 */
      if (frame.arg >= fn.constants.size() ||
          fn.constants[frame.arg].is_number) {
        error = fail("Error: SuperLoadProp requires a string constant"); break;
      }
      const std::string &key = fn.constants[frame.arg].str;
      JSObject *proto = nullptr;
      if (frame.self != nullptr && frame.self->homeObject() != nullptr) {
        proto = frame.self->homeObject()->prototype();
      }
      if (proto == nullptr) {
        error = fail("TypeError: Cannot read properties of super"); break;
      }
      JSValue *val = nullptr;
      if (proto->isMap() && key == "size") {
        val = frame.ctx->createValue(static_cast<double>(proto->collectionSize()));
      } else {
      /* M4.4h：访问器感知读取（数据属性直接取值；访问器调 getter
      * this=frame_this）。getPropertyValue 的 target 为 HomeObject 原型
      * 链起点，但访问器 this 须是当前方法 this——此处特判：访问器
      * 属性手动调 getter（this=frame_this）。 */
        PropertyDescriptor *desc = findPropertyDescriptor(proto, key);
        if (desc != nullptr && desc->type == PropertyDescriptor::Type::Accessor) {
          if (desc->getter == nullptr || !isCallable(desc->getter)) {
            val = frame.ctx->createValue(Kind::Undefined);
          } else {
            JSValue *this_obj =
                frame_this != nullptr ? frame_this : frame.ctx->globalObject();
            ConversionResult r = call(frame.ctx, desc->getter, this_obj, nullptr, 0);
            if (!r.ok()) { error = std::move(r); break; }
            val = r.value;
          }
        } else {
          ConversionResult gv = getPropertyValue(frame.ctx, proto, key);
          if (!gv.ok()) return gv;
          val = gv.value;
        }
      }
      stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_SuperLoadElem(VmFrame &frame) {
  auto &stack = *frame.stack;
  JSValue *&frame_this = *frame.frame_this;
  ConversionResult error;
  do {
      /* M5.1d-b super[expr]：从 HomeObject.[[Prototype]] 读计算键。
      * 键运行期 ToPropertyKey（§7.1.14）；访问器 this = 当前帧 this。 */
      if (stack.empty()) { error = fail("Error: stack underflow on SuperLoadElem"); break; }
      JSValue *key = stack.back();
      JSObject *proto = nullptr;
      if (frame.self != nullptr && frame.self->homeObject() != nullptr) {
        proto = frame.self->homeObject()->prototype();
      }
      if (proto == nullptr) {
        stack.pop_back();
        error = fail("TypeError: Cannot read properties of super"); break;
      }
      ConversionResult pk = toPropertyKey(frame.ctx, key);
      if (!pk.ok()) return pk;
      JSValue *val = nullptr;
      if (pk.value->kind() == Kind::Symbol) {
        val = proto->getSymbolProperty(pk.value);
      } else {
        const std::string &name = pk.value->toString();
        PropertyDescriptor *desc = findPropertyDescriptor(proto, name);
        if (desc != nullptr && desc->type == PropertyDescriptor::Type::Accessor) {
          if (desc->getter == nullptr || !isCallable(desc->getter)) {
            val = frame.ctx->createValue(Kind::Undefined);
          } else {
            JSValue *this_obj =
                frame_this != nullptr ? frame_this : frame.ctx->globalObject();
            ConversionResult r = call(frame.ctx, desc->getter, this_obj, nullptr, 0);
            if (!r.ok()) { error = std::move(r); break; }
            val = r.value;
          }
        } else {
          ConversionResult gv = getPropertyValue(frame.ctx, proto, name);
          if (!gv.ok()) return gv;
          val = gv.value;
        }
      }
      stack.pop_back();
      stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_SuperCall(VmFrame &frame) {
  auto &stack = *frame.stack;
  JSValue *&frame_this = *frame.frame_this;
  ConversionResult error;
  do {
      /* M5.1d-c1 super()：调用基类构造器于当前帧 this。
      * 基类构造器 = HomeObject.[[Prototype]]（构造器 HomeObject 是
      * 自身，extends 后其 [[Prototype]] 是基类构造器）。
      * 语义（§9.2.2.4 derived）：在现有 this 上执行基类构造器；
      * 基类构造器显式返回对象时以该对象作为新 this（覆盖）。 */
      uint16_t argc = frame.arg;
      size_t args_pos = stack.size() - static_cast<size_t>(argc);
      if (stack.size() < static_cast<size_t>(argc)) {
        error = fail("Error: stack underflow on SuperCall"); break;
      }
      JSObject *home = (frame.self != nullptr) ? frame.self->homeObject() : nullptr;
      if (home == nullptr) {
        error = fail("TypeError: super() can only be used in a derived constructor"); break;
      }
      JSObject *base = home->prototype(); /* 基类构造器 */
      if (base == nullptr) {
        error = fail("TypeError: cannot call super() on a base class"); break;
      }
      JSValue *base_val = base->holder();
      if (base_val == nullptr || !isCallable(base_val)) {
        error = fail("TypeError: super is not a constructor"); break;
      }
      JSValue *const *call_args =
          argc > 0 ? &stack[args_pos] : nullptr;
      JSValue *this_obj =
          frame_this != nullptr ? frame_this : frame.ctx->globalObject();
      ConversionResult r = call(frame.ctx, base_val, this_obj, call_args, argc);
      if (!r.ok()) { error = std::move(r); break; }
      /* 基类构造器返回对象 -> 新 this（覆盖 frame_this）。
      * frame_this 更新后，后续 LoadThis 引用新 this。 */
      if (r.value != nullptr && r.value->isObject()) {
        frame_this = r.value;
      }
      /* 收缩：弹出 argc 个参数，压入 this（super() 求值为 this）。
      * M5.1d-c2 派生类构造器：私有字段初始化指令已由编译器在 super()
      * 之后内联发射（compileCall SuperCall 分支），本指令无需运行时钩子。 */
      stack.resize(args_pos);
      stack.push_back(frame_this);
      if (r.tmp_handle != 0) frame.ctx->handles().free(r.tmp_handle);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_SuperCallArray(VmFrame &frame) {
  auto &stack = *frame.stack;
  JSValue *&frame_this = *frame.frame_this;
  ConversionResult error;
  do {
      /* M5.1d-d：super(...args) 展开调用——派生类默认构造器转发全部
      * 实参给基类构造器（栈顶为 args 数组）。语义同 SuperCall，
      * 参数从数组扁平化（空洞 -> undefined）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on SuperCallArray"); break; }
      JSValue *args_arr = stack.back();
      if (args_arr == nullptr || !args_arr->isObject() ||
          !args_arr->object()->isArray()) {
        error = fail("TypeError: super spread argument must be iterable"); break;
      }
      JSObject *aobj = args_arr->object();
      std::vector<JSValue *> flat;
      flat.reserve(aobj->length());
      for (size_t i = 0; i < aobj->length(); ++i) {
        JSValue *e = aobj->getElement(i);
        flat.push_back(e != nullptr ? e : frame.ctx->createValue(Kind::Undefined));
      }
      JSObject *home = (frame.self != nullptr) ? frame.self->homeObject() : nullptr;
      if (home == nullptr) {
        error = fail("TypeError: super() can only be used in a derived constructor"); break;
      }
      JSObject *base = home->prototype(); /* 基类构造器 */
      if (base == nullptr) {
        error = fail("TypeError: cannot call super() on a base class"); break;
      }
      JSValue *base_val = base->holder();
      if (base_val == nullptr || !isCallable(base_val)) {
        error = fail("TypeError: super is not a constructor"); break;
      }
      JSValue *this_obj =
          frame_this != nullptr ? frame_this : frame.ctx->globalObject();
      ConversionResult r =
          call(frame.ctx, base_val, this_obj,
               flat.empty() ? nullptr : flat.data(), flat.size());
      if (!r.ok()) { error = std::move(r); break; }
      if (r.value != nullptr && r.value->isObject()) {
        frame_this = r.value;
      }
      /* 收缩：弹出 args 数组，压入 this。 */
      stack.pop_back();
      stack.push_back(frame_this);
      if (r.tmp_handle != 0) frame.ctx->handles().free(r.tmp_handle);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadPrivate(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1d-c2：receiver -> 私有字段值。
      * 字段全名 = fn.private_names[instr.arg]（含 '#' 前缀）。
      * 接收者校验：无该私有槽 -> TypeError（§8.3 PrivateFieldFind）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on LoadPrivate"); break; }
      if (frame.arg >= fn.private_names.size()) {
        error = fail("Error: private field index out of range"); break;
      }
      const std::string &field = fn.private_names[frame.arg];
      JSValue *receiver = stack.back();
      if (!receiver->isObject()) {
        error = fail("TypeError: Cannot read private member " + field +
                    " from an object whose class did not declare it"); break;
      }
      JSObject *obj = receiver->object();
      if (!obj->hasPrivateSlot(field)) {
        error = fail("TypeError: Cannot read private member " + field +
                    " from an object whose class did not declare it"); break;
      }
      JSValue *val = obj->getPrivateSlot(field);
      stack.pop_back();
      stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StorePrivate(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1d-c2：receiver, val -> val（obj.#x = val）。接收者校验同 LoadPrivate。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on StorePrivate"); break; }
      if (frame.arg >= fn.private_names.size()) {
        error = fail("Error: private field index out of range"); break;
      }
      const std::string &field = fn.private_names[frame.arg];
      JSValue *val = stack.back();
      JSValue *receiver = stack[stack.size() - 2];
      if (!receiver->isObject()) {
        error = fail("TypeError: Cannot write private member " + field +
                    " to an object whose class did not declare it"); break;
      }
      JSObject *obj = receiver->object();
      if (!obj->hasPrivateSlot(field)) {
        error = fail("TypeError: Cannot write private member " + field +
                    " to an object whose class did not declare it"); break;
      }
      obj->setPrivateSlot(field, val);
      stack.resize(stack.size() - 2);
      stack.push_back(val);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_InitPrivate(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1d-c2：构造器字段初始化 receiver, val -> val（InitPrivate {name_index}）。
      * 与 StorePrivate 的区别：不要求接收者已声明该槽——实例初始化阶段
      * （§12.2.9 InitializeInstanceElements）首次为对象创建私有槽。
      * 接收者非对象 -> TypeError（§8.3 PrivateFieldAdd 步骤 2）。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on InitPrivate"); break; }
      if (frame.arg >= fn.private_names.size()) {
        error = fail("Error: private field index out of range"); break;
      }
      const std::string &field = fn.private_names[frame.arg];
      JSValue *val = stack.back();
      JSValue *receiver = stack[stack.size() - 2];
      if (!receiver->isObject()) {
        error = fail("TypeError: Cannot initialize private member " + field +
                    " on a non-object"); break;
      }
      receiver->object()->setPrivateSlot(field, val);
      stack.resize(stack.size() - 2);
      stack.push_back(val);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_HasPrivate(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1d-c2：#x in obj -> bool（receiver 替换为布尔）。
      * 接收者不是对象 -> TypeError（§8.3 PrivateFieldFind 步骤 1）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on HasPrivate"); break; }
      if (frame.arg >= fn.private_names.size()) {
        error = fail("Error: private field index out of range"); break;
      }
      const std::string &field = fn.private_names[frame.arg];
      JSValue *receiver = stack.back();
      if (!receiver->isObject()) {
        error = fail("TypeError: Cannot use 'in' operator to search for "
                    "private field " + field + " in a non-object"); break;
      }
      bool found = receiver->object()->hasPrivateSlot(field);
      stack.pop_back();
      stack.push_back(frame.ctx->createValue(found));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_In(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e：x in y（§7.3.20 HasProperty）。栈 [x, y] -> bool。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on In"); break; }
      JSValue *y = stack.back();
      JSValue *x = stack[stack.size() - 2];
      ConversionResult r = inOperator(frame.ctx, x, y);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Instanceof(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e：x instanceof y（§7.3.22 InstanceofOperator）。栈 [x, y] -> bool。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on Instanceof"); break; }
      JSValue *y = stack.back();
      JSValue *x = stack[stack.size() - 2];
      ConversionResult r = instanceOf(frame.ctx, x, y);
      if (!r.ok()) { error = std::move(r); break; }
      stack.resize(stack.size() - 2);
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_ToString(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e：一元转换 ToString(x)（模板串插值）。Symbol -> TypeError。 */
      if (stack.empty()) { error = fail("Error: stack underflow on ToString"); break; }
      JSValue *a = stack.back();
      ConversionResult r = toString(frame.ctx, a);
      if (!r.ok()) { error = std::move(r); break; }
      stack.pop_back();
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_ToNumber(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e：一元 +x（§7.1.4 ToNumber）。BigInt/Symbol -> TypeError。 */
      if (stack.empty()) { error = fail("Error: stack underflow on ToNumber"); break; }
      JSValue *a = stack.back();
      ConversionResult r = toNumber(frame.ctx, a);
      if (!r.ok()) { error = std::move(r); break; }
      stack.pop_back();
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_BitNot(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e：一元 ~x（§13.5.5）。ToNumeric 后按位非（BigInt 走任意精度）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on BitNot"); break; }
      JSValue *a = stack.back();
      ConversionResult r = unaryBitNot(frame.ctx, a);
      if (!r.ok()) { error = std::move(r); break; }
      stack.pop_back();
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_DeleteProp(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e：delete obj.prop（§13.5.1.2，v1 非严格简化：own 属性删除成功
      * 返回 true；非 own / 无该属性返回 true——§13.5.1.4 步骤 3：无属性
      * 也返回 true；沿原型链的属性返回 true（仅删除 own）。
      * 栈 [obj] -> bool。 */
      if (stack.empty()) { error = fail("Error: stack underflow on DeleteProp"); break; }
      if (frame.arg >= fn.constants.size() ||
          fn.constants[frame.arg].is_number) {
        error = fail("Error: DeleteProp requires a string constant"); break;
      }
      JSValue *obj = stack.back();
      bool deleted = true;
      if (obj->isObject()) {
        const std::string &name = fn.constants[frame.arg].str;
        JSObject *o = obj->object();
        if (o->hasOwnProperty(name)) {
          deleted = o->deleteProperty(name);
        }
      }
      stack.pop_back();
      stack.push_back(frame.ctx->createValue(deleted));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_DeleteElem(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e：delete obj[expr]（§13.5.1.3）。栈 [obj, key] -> bool。
      * key ToPropertyKey 后同上（非 own/无属性返回 true）。 */
      if (stack.size() < 2) { error = fail("Error: stack underflow on DeleteElem"); break; }
      JSValue *key = stack.back();
      JSValue *obj = stack[stack.size() - 2];
      bool deleted = true;
      if (obj->isObject()) {
        ConversionResult pk = toPropertyKey(frame.ctx, key);
        if (!pk.ok()) return pk;
        JSObject *o = obj->object();
        const std::string &name = pk.value->toString();
      /* 数组整数索引：删元素（长度不变）。 */
        bool numeric = !name.empty() &&
                       name.find_first_not_of("0123456789") == std::string::npos;
        if (o->isArray() && numeric && name.size() <= 18) {
          size_t idx = static_cast<size_t>(std::strtoull(name.c_str(), nullptr, 10));
          if (o->hasElement(idx)) {
            o->deleteElement(idx);
          }
        } else if (o->hasOwnProperty(name)) {
          deleted = o->deleteProperty(name);
        }
      }
      stack.resize(stack.size() - 2);
      stack.push_back(frame.ctx->createValue(deleted));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_EnumerateKeys(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e-c for-in：栈顶对象 -> 键数组（§7.3.24）。
      * ToObject 装箱（undefined/null -> TypeError）；收集自身 + 原型链
      * 可枚举字符串键为新数组（键为字符串值）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on EnumerateKeys"); break; }
      ConversionResult boxed = toObject(frame.ctx, stack.back());
      if (!boxed.ok()) return boxed;
      JSObject *o = boxed.value->object();
      std::vector<std::string> keys;
      enumerateObjectKeys(o, &keys);
      JSValue *arr = frame.ctx->createArray(keys.size());
      /* 分配保护（M3.3）：fill 期间 createValue 分配窗口触发 GC 会
      * 误回收尚未入栈的 arr（悬垂）。pin 到压栈后解除。 */
      frame.ctx->heap().pin(arr);
      for (size_t i = 0; i < keys.size(); ++i) {
        arr->object()->setElement(i, frame.ctx->createValue(keys[i]));
      }
      stack.pop_back();
      stack.push_back(arr);
      frame.ctx->heap().unpin(arr); /* 已入 VM 栈（根集保护） */
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_RestObjectProps(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e-d 对象 rest 解构：{a, ...rest} = src。
      * 栈 [src, exclude_arr]（exclude_arr = 已绑定键的字符串数组）。
      * 弹两值：复制 src 的 own enumerable 字符串属性到槽内目标对象，
      * 跳过 exclude_arr 中的键（CopyDataProperties 步骤 4b 的排除集合）。
      * 语义（§13.15.5）：先收集源 own enumerable 键，再移除已被模式
      * 绑定的键，剩余复制。null/undefined 源 -> TypeError（rest 必须
      * 可解构对象，与对象展开的跳过不同）。 */
      if (stack.size() < 2 || frame.arg >= fn.numLocals) {
        error = fail("Error: stack underflow on RestObjectProps"); break;
      }
      JSValue *exclude = stack.back();
      JSValue *src = stack[stack.size() - 2];
      stack.resize(stack.size() - 2);
      JSValue *target = stack[frame.arg];
      if (target == nullptr || !target->isObject()) {
        error = fail("TypeError: object rest target is not an object"); break;
      }
      if (src == nullptr || src->kind() == Kind::Undefined ||
          src->kind() == Kind::Null) {
        std::string tn = (src == nullptr || src->kind() == Kind::Undefined)
                             ? "undefined"
                             : "null";
        error = fail("TypeError: Cannot destructure " + tn + " as it is null"); break;
      }
      ConversionResult boxed = toObject(frame.ctx, src);
      if (!boxed.ok()) return boxed;
      JSObject *sobj = boxed.value->object();
      JSObject *tobj = target->object();
      /* 排除键集合：exclude_arr 元素（字符串）收集为集合。 */
      std::unordered_set<std::string> excluded;
      if (exclude != nullptr && exclude->isObject() &&
          exclude->object()->isArray()) {
        JSObject *ea = exclude->object();
        for (size_t i = 0; i < ea->length(); ++i) {
          JSValue *k = ea->getElement(i);
          if (k != nullptr && k->kind() == Kind::String) {
            excluded.insert(k->toString());
          }
        }
      }
      /* 收集源 own enumerable 键（同 CopyObjectProps：数组整数索引 +
      * String 包装虚拟索引 + propertyOrder 过滤 enumerable）。 */
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
        if (excluded.count(k) > 0) continue; /* 已绑定键跳过 */
        JSValue *v = nullptr;
        if (sobj->isArray()) {
          size_t idx = static_cast<size_t>(std::strtoull(k.c_str(), nullptr, 10));
          v = sobj->getElement(idx);
        } else {
          PropertyDescriptor *d = sobj->getOwnProperty(k);
          if (d != nullptr && d->type == PropertyDescriptor::Type::Accessor) {
            if (d->getter == nullptr || !isCallable(d->getter)) {
              v = frame.ctx->createValue(Kind::Undefined);
            } else {
              ConversionResult r = call(frame.ctx, d->getter, sobj->holder(), nullptr, 0);
              if (!r.ok()) { error = std::move(r); break; }
              v = r.value;
            }
          } else {
            v = d != nullptr && d->value != nullptr ? d->value
                                                    : frame.ctx->createValue(Kind::Undefined);
          }
        }
        bool numeric = !k.empty() &&
                       k.find_first_not_of("0123456789") == std::string::npos;
        if (tobj->isArray() && numeric && k.size() <= 18) {
          tobj->setElement(static_cast<size_t>(std::strtoull(k.c_str(), nullptr, 10)), v);
        } else {
          tobj->setProperty(k, v);
        }
      }
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_NextKey(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e-c for-in：栈顶键数组 -> 弹首键（并移除）或 undefined（空）。
      * 键数组按 [[OwnPropertyKeys]] 序填充（升序/创建序），故从头部弹。
      * 键数组为普通 JS 数组：读首元素，前移移除。 */
      if (stack.empty()) { error = fail("Error: stack underflow on NextKey"); break; }
      JSValue *arr = stack.back();
      if (arr->isObject() && arr->object()->isArray()) {
        JSObject *a = arr->object();
        size_t n = a->length();
        JSValue *key = nullptr;
        if (n > 0) {
          key = a->getElement(0);
          for (size_t i = 1; i < n; ++i) {
            a->setElement(i - 1, a->getElement(i));
          }
          a->resizeElements(n - 1);
        }
        stack.pop_back();
        stack.push_back(key != nullptr ? key : frame.ctx->createValue(Kind::Undefined));
      } else {
        stack.pop_back();
        stack.push_back(frame.ctx->createValue(Kind::Undefined));
      }
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Increment(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* M5.1e-b：一元 ++/--（§13.4.4）。arg=0 增 1，arg=1 减 1。
      * ToNumeric 后 ±1（BigInt ±1n，Number ±1）。 */
      if (stack.empty()) { error = fail("Error: stack underflow on Increment"); break; }
      JSValue *a = stack.back();
      ConversionResult r = unaryIncrement(frame.ctx, a, frame.arg != 0);
      if (!r.ok()) { error = std::move(r); break; }
      stack.pop_back();
      stack.push_back(r.value);
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_LoadUpvalue(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 捕获的外层变量读取（M2.5）：沿环境链 depth 跳转后查 name。 */
      JSEnvironment *e = captureEnv(frame.env, fn, frame.arg);
      if (e == nullptr || frame.arg >= fn.captures.size()) {
        error = fail("ReferenceError: captured variable not found"); break;
      }
      const std::string &name = fn.captures[frame.arg].name;
      JSValue *val = e->getBinding(name);
      stack.push_back(val != nullptr ? val : frame.ctx->createValue(Kind::Undefined));
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_StoreUpvalue(VmFrame &frame) {
  const FunctionBytecode &fn = *frame.fn;
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* 捕获的外层变量写入（M2.5）：沿环境链 depth 跳转后写回。 */
      if (stack.empty()) {
        error = fail("Error: stack underflow on StoreUpvalue"); break;
      }
      JSEnvironment *e = captureEnv(frame.env, fn, frame.arg);
      if (e == nullptr || frame.arg >= fn.captures.size()) {
        error = fail("ReferenceError: captured variable not found"); break;
      }
      const std::string &name = fn.captures[frame.arg].name;
      JSValue *val = stack.back();
      stack.pop_back();
      e->setBinding(name, val); /* 值写回目标环境 */
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Throw(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
      /* JS throw（§14.15）：栈顶值作为异常抛出（弹栈，异常传播）。
      * 值经 error.thrown 携带（JS throw 值），异常表/跨帧拦截。 */
      if (stack.empty()) {
        error = fail("Error: stack underflow on Throw");
        break;
      }
      JSValue *v = stack.back();
      stack.pop_back();
      error.value = nullptr;
      error.error.clear();
      error.thrown = v;
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_RethrowPending(VmFrame &frame) {
  ConversionResult error;
  do {
      /* finally 末尾（§14.16.4）：有挂起异常则重抛，否则正常继续。
       * M5.5b：return completion（pending_is_return=true）不重抛——
       * finally 已执行，用 return value 完成函数：retval 压栈 + 置 done，
       * runBytecode 据栈顶取返回值，实现 return 穿透 finally 语义
       * （§13.10.5 step 4）。 */
      if (frame.has_pending) {
        if (frame.pending_is_return) {
          /* return completion：finally 已执行，用 return value 完成。
           * retval 压栈（成为栈根集一部分）后再释放临时句柄——顺序关键：
           * 先释放句柄再压栈，窗口期内 GC 可能回收 retval（悬垂）。
           * 置 done 让主循环结束本帧；runBytecode 据栈顶取返回值。 */
          JSValue *retval = frame.pending.thrown;
          if (retval == nullptr) retval = frame.ctx->createValue(Kind::Undefined);
          frame.stack->push_back(retval);
          frame.has_pending = false;
          frame.pending_is_return = false;
          if (frame.pending_handle != 0) {
            frame.ctx->handles().free(frame.pending_handle);
            frame.pending_handle = 0;
          }
          frame.done = true;
          return ok(nullptr);
        }
        frame.has_pending = false;
        error = std::move(frame.pending);
        if (frame.pending_handle != 0) {
          frame.ctx->handles().free(frame.pending_handle);
          frame.pending_handle = 0;
        }
        break;
      }
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Await(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    if (stack.empty()) { error = fail("Error: stack underflow on Await"); break; }
    JSValue *x = stack.back();
    /* §27.7.4.1 Await 步骤 2-3：PromiseResolve(%Promise%, x)。
     * x 是 Promise 直接用；否则包一层（Promise.resolve(x)）接合。 */
    JSValue *awaitable = x;
    if (!(x->isObject() && x->object()->isPromise())) {
      JSValue *resolve = nullptr, *reject = nullptr;
      JSValue *p = createDerivedPromise(frame.ctx, &resolve, &reject);
      frame.ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
      JSValue *argv[1] = {x};
      ConversionResult rr =
          call(frame.ctx, resolve, frame.ctx->createValue(Kind::Undefined),
               argv, 1);
      frame.ctx->heap().unpin(p);
      if (!rr.ok()) { error = std::move(rr); break; }
      awaitable = p;
    }
    JSObject *po = awaitable->object();
    const int st = po->promiseState();
    if (st == static_cast<int>(PromiseState::Fulfilled)) {
      /* 已 fulfilled：栈顶替换为 result，继续执行。 */
      stack.back() = po->promiseResult();
      break;
    }
    if (st == static_cast<int>(PromiseState::Rejected)) {
      /* 已 rejected：await 抛 rejection 值（异常传播）。 */
      JSValue *reason = po->promiseResult();
      stack.pop_back();
      error.value = nullptr;
      error.error.clear();
      error.thrown = reason;
      break;
    }
    /* pending：挂起（M5.4）。保存现场到 AsyncFrame + 入队续延 job。 */
    JSFunction *self = frame.self;
    if (self == nullptr) {
      error = fail("Error: await outside async function");
      break;
    }
    if (!suspendAwait(frame.ctx, self, *frame.pc, stack, frame.env,
                      *frame.frame_this, frame.pending, frame.has_pending,
                      frame.pending_handle, awaitable)) {
      error = fail("Error: failed to suspend async frame");
      break;
    }
    frame.suspended = true;
    frame.done = true;
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

static ConversionResult op_Nop(VmFrame &frame) {
  ConversionResult error;
  do {
      break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* M5.5a-3 Yield 指令：generator 函数挂起（§27.5.3.2 GeneratorYield）。
 *   - 弹栈顶 yield 值（yield <expr> 的求值结果；裸 yield 则 undefined）
 *   - 沿 env 链查找隐藏绑定 __generator__（generatorCall 写入），定位
 *     当前 generator 对象
 *   - 调 suspendYield 把 VM 现场保存到 Generator 内嵌的 AsyncFrame
 *     （pc=yield 下一条、stack/env/this/pending 拷贝、yield_value=弹栈值）
 *   - 置 frame.suspended + frame.done，结束本帧 runBytecode；
 *     resumeGenerator 检测 suspended 后包装 {value:yield_value, done:false}
 *     返回给 next() 调用方，并置 Generator.state=suspended-yield
 * 恢复时（next/throw/return）从 AsyncFrame 重建现场续延，next 把传入值
 * 压栈作为 yield 表达式的值（§27.5.3.1 GeneratorResume step 9）。 */
static ConversionResult op_Yield(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    /* 1. 弹栈顶 yield 值。空栈视为裸 yield（undefined）。 */
    JSValue *yield_val = nullptr;
    if (stack.empty()) {
      yield_val = frame.ctx->createValue(Kind::Undefined);
    } else {
      yield_val = stack.back();
      stack.pop_back();
    }

    /* 2. 沿 env 链查找隐藏绑定 __generator__，定位当前 generator。 */
    JSValue *gen_v = nullptr;
    for (JSEnvironment *e = frame.env; e != nullptr; e = e->parent()) {
      if (e->hasBinding(kGeneratorBinding)) {
        gen_v = e->getBinding(kGeneratorBinding);
        break;
      }
    }
    if (gen_v == nullptr || !gen_v->isObject() ||
        (!gen_v->object()->isGenerator() &&
         !gen_v->object()->isAsyncGenerator())) {
      error = fail("SyntaxError: yield outside of generator function");
      break;
    }

    /* 3. 保存现场到 Generator/AsyncGenerator 内嵌的 AsyncFrame，置帧挂起。
     *    M5.5c-1 async generator：走 suspendAsyncYield——resolve 当前请求
     *    capability {value,done:false} + 保存现场 + 置 SuspendedYield（
     *    AsyncGeneratorYield，§27.6.3.4）；同步 generator 走 suspendYield。
     *    next_pc = 当前 pc（已指向 yield 下一条指令，主循环自增前）。 */
    JSFunction *self = frame.self;
    if (self == nullptr) {
      error = fail("Error: yield with no function context");
      break;
    }
    if (gen_v->object()->isAsyncGenerator()) {
      if (!suspendAsyncYield(frame.ctx, gen_v, self, *frame.pc, stack,
                             frame.env, *frame.frame_this, frame.pending,
                             frame.has_pending, frame.pending_handle,
                             frame.pending_is_return, yield_val)) {
        error = fail("Error: failed to suspend async generator frame");
        break;
      }
    } else if (!suspendYield(frame.ctx, gen_v, self, *frame.pc, stack,
                             frame.env, *frame.frame_this, frame.pending,
                             frame.has_pending, frame.pending_handle,
                             frame.pending_is_return, yield_val)) {
      error = fail("Error: failed to suspend generator frame");
      break;
    }
    frame.suspended = true;
    frame.done = true;
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* M5.5a-4 YieldDelegate 指令：yield* <expr> 委托（§14.4 / §27.5.3.3）。
 *   - 弹栈顶 iterable（yield* 的操作数，已求值）
 *   - 沿 env 链查找隐藏绑定 __generator__（同 op_Yield），定位当前 generator
 *   - 调 yieldDelegateStart：getIterator(iterable) + inner.next() 首步
 *     * inner done -> inner.value 压回栈顶（yield* 表达式结果），非挂起，
 *       外层字节码继续执行（不结束本帧 runBytecode）；
 *     * inner yield -> frame.yield_value = inner.value，返回 suspended=true，
 *       置 frame.suspended + frame.done 结束本帧；resumeGenerator 检测后
 *       包装 {value,done:false} 返回给 next() 调用方，置 state=suspended-yield；
 *     * inner 抛错 -> 透传 thrown/error（外层 try/catch 可捕）。 */
static ConversionResult op_YieldDelegate(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    /* 1. 弹栈顶 iterable。空栈视为 undefined（yield* undefined 由 getIterator
     *    报 TypeError "undefined is not iterable"）。 */
    JSValue *iterable = nullptr;
    if (stack.empty()) {
      iterable = frame.ctx->createValue(Kind::Undefined);
    } else {
      iterable = stack.back();
      stack.pop_back();
    }

    /* 2. 沿 env 链查找隐藏绑定 __generator__，定位当前 generator。 */
    JSValue *gen_v = nullptr;
    for (JSEnvironment *e = frame.env; e != nullptr; e = e->parent()) {
      if (e->hasBinding(kGeneratorBinding)) {
        gen_v = e->getBinding(kGeneratorBinding);
        break;
      }
    }
    if (gen_v == nullptr || !gen_v->isObject() ||
        !gen_v->object()->isGenerator()) {
      error = fail("SyntaxError: yield* outside of generator function");
      break;
    }

    /* 3. 调 yieldDelegateStart（getIterator + inner.next 首步）。
     * 注意：suspended 路径下 yieldDelegateStart 返回 value=nullptr
     * （ConversionResult::ok() 据 value!=nullptr 判定，故 suspended 时
     * ok()=false——不能用 !ok() 判失败，需先排除 suspended）。 */
    ConversionResult r = yieldDelegateStart(frame.ctx, gen_v, iterable);
    if (r.suspended) {
      /* inner yield：挂起外层 generator。yieldDelegateStart 已写入
       * frame->yield_value（inner yield 值）和 frame->delegate_iterator。
       * 必须调 suspendYield 保存 VM 现场到 AsyncFrame（pc/stack/env/this/
       * pending）——否则恢复时从过时 pc 续延导致执行错乱。delegate_iterator
       * 被 suspendYield 保留（其不触及该字段）。 */
      AsyncFrame *af = asAsyncFrame(gen_v->object()->generatorContext());
      JSValue *yv = (af != nullptr) ? af->yield_value
                                    : frame.ctx->createValue(Kind::Undefined);
      if (!suspendYield(frame.ctx, gen_v, frame.self, *frame.pc, stack,
                        frame.env, *frame.frame_this, frame.pending,
                        frame.has_pending, frame.pending_handle,
                        frame.pending_is_return, yv)) {
        error = fail("Error: failed to suspend generator frame on yield*");
        break;
      }
      frame.suspended = true;
      frame.done = true;
    } else if (!r.ok()) {
      /* inner 抛错（非挂起）：透传 thrown/error。 */
      error = std::move(r);
      break;
    } else {
      /* inner done：yieldDelegateStart 已把 inner.value 压入 AsyncFrame.stack。
       * 非挂起路径需把 inner.value 同步到 *frame.stack（当前操作数栈），
       * 外层字节码续延从栈顶取 yield* 表达式结果。AsyncFrame.stack 是
       * frame.stack 的副本（仅 suspendYield 时拷贝），此处非挂起路径直接
       * 同步末尾元素。 */
      AsyncFrame *af = asAsyncFrame(gen_v->object()->generatorContext());
      if (af != nullptr && !af->stack.empty()) {
        stack.push_back(af->stack.back());
        af->stack.pop_back();
      }
    }
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* M5.5e AsyncYieldDelegate 指令：async generator 内 yield* <expr> 委托
 * async 迭代器（§14.4.14 + §27.6.3.6）。
 *   - 弹栈顶 iterable（yield* 的操作数，已求值）
 *   - 沿 env 链查找隐藏绑定 __generator__，定位当前 async generator
 *     （非 async generator 报 SyntaxError——compiler 保证只在 async
 *     generator 内发此指令，防御性校验）
 *   - getAsyncIterator(iterable)（优先 @@asyncIterator，回退 @@iterator
 *     包装 AsyncFromSyncIterator）-> inner async iterator
 *   - 保存 VM 现场到 AsyncFrame（委托挂起基点），设 delegate_iterator +
 *     delegate_phase=AwaitNext
 *   - 首次 inner.next(undefined) -> promise -> PromiseResolve 接合 ->
 *     asyncDelegateAwait（挂起或同步 settle）
 *     * pending   -> 置 frame.suspended 结束本帧（AsyncDelegateResolve/Reject
 *       恢复时 asyncDelegateResume 驱动委托状态机）
 *     * resolve   -> iter_result：done=true 压 inner.value 清 delegate 非挂起
 *       继续外层；done=false asyncDelegateYieldValue（先 await 值再 yield）
 *     * reject    -> 清 delegate，异常透传（外层 try/catch 可捕） */
static ConversionResult op_AsyncYieldDelegate(VmFrame &frame) {
  auto &stack = *frame.stack;
  ConversionResult error;
  do {
    /* 1. 弹栈顶 iterable。空栈视为 undefined（yield* undefined 由
     *    getAsyncIterator 报 TypeError "obj is not async iterable"）。 */
    JSValue *iterable = nullptr;
    if (stack.empty()) {
      iterable = frame.ctx->createValue(Kind::Undefined);
    } else {
      iterable = stack.back();
      stack.pop_back();
    }

    /* 2. 沿 env 链查找隐藏绑定 __generator__，定位当前 async generator。 */
    JSValue *gen_v = nullptr;
    for (JSEnvironment *e = frame.env; e != nullptr; e = e->parent()) {
      if (e->hasBinding(kGeneratorBinding)) {
        gen_v = e->getBinding(kGeneratorBinding);
        break;
      }
    }
    if (gen_v == nullptr || !gen_v->isObject() ||
        !gen_v->object()->isAsyncGenerator()) {
      error = fail("SyntaxError: yield* outside of async generator function");
      break;
    }

    /* 3. getAsyncIterator（优先 @@asyncIterator，回退 @@iterator 包装
     *    AsyncFromSyncIterator，§14.7.5.1）。yield* 42 报 TypeError。 */
    ConversionResult it = getAsyncIterator(frame.ctx, iterable);
    if (!it.ok()) {
      error = std::move(it);
      break;
    }

    /* 4. 保存 VM 现场到 AsyncFrame + 设 delegate。现场是委托挂起/恢复的
     *    基点（asyncDelegateAwait 挂起不覆盖现场）。 */
    AsyncFrame *af = asAsyncFrame(gen_v->object()->asyncGeneratorContext());
    if (af == nullptr || frame.self == nullptr) {
      error = fail("Error: invalid async generator context on yield*");
      break;
    }
    saveAsyncFrameForDelegate(frame.ctx, af, frame.self, *frame.pc, stack,
                              frame.env, *frame.frame_this, frame.pending,
                              frame.has_pending, frame.pending_handle,
                              frame.pending_is_return);
    /* delegate_iterator 写 frame 前防误回收（getAsyncIterator 返回后可能
     * 触发 GC）。 */
    frame.ctx->heap().pin(it.value);
    af->delegate_iterator = it.value;
    frame.ctx->heap().unpin(it.value);
    af->delegate_phase = AsyncDelegatePhase::AwaitNext;

    /* 5. 首次 inner.next(undefined)。 */
    JSValue *next_key = frame.ctx->createValue(std::string("next"));
    ConversionResult m = getMethod(frame.ctx, it.value, next_key);
    if (m.value == nullptr &&
        (!m.error.empty() || m.thrown != nullptr)) {
      af->delegate_iterator = nullptr;
      af->delegate_phase = AsyncDelegatePhase::None;
      error = std::move(m);
      break;
    }
    if (m.value == nullptr) {
      af->delegate_iterator = nullptr;
      af->delegate_phase = AsyncDelegatePhase::None;
      error = fail("TypeError: iterator.next is not a function");
      break;
    }
    JSValue *undef = frame.ctx->createValue(Kind::Undefined);
    JSValue *argv[1] = {undef};
    ConversionResult c = call(frame.ctx, m.value, it.value, argv, 1);
    if (!c.ok()) {
      /* inner.next 同步抛错：清 delegate，透传异常。 */
      af->delegate_iterator = nullptr;
      af->delegate_phase = AsyncDelegatePhase::None;
      error = std::move(c);
      break;
    }

    /* 6. PromiseResolve 接合 + await（挂起或同步 settle）。 */
    JSValue *p = delegatePromiseResolve(frame.ctx, c.value);
    ConversionResult r =
        asyncDelegateAwait(frame.ctx, af, gen_v, p, AsyncDelegatePhase::AwaitNext);
    if (r.suspended) {
      /* 挂起：现场已保存，AsyncDelegateResolve/Reject 恢复时驱动委托状态机。 */
      frame.suspended = true;
      frame.done = true;
      break;
    }
    if (!r.ok()) {
      /* inner.next 结果 reject：清 delegate，透传异常。 */
      af->delegate_iterator = nullptr;
      af->delegate_phase = AsyncDelegatePhase::None;
      error = std::move(r);
      break;
    }

    /* 7. 同步 settle：iter_result = r.value。 */
    if (r.value == nullptr || !r.value->isObject()) {
      af->delegate_iterator = nullptr;
      af->delegate_phase = AsyncDelegatePhase::None;
      error = fail("TypeError: iterator result is not an object");
      break;
    }
    if (delegateIterResultDone(r.value)) {
      /* inner done：压 inner.value 到栈（yield* 表达式结果），清 delegate，
       * 非挂起继续外层字节码。 */
      JSValue *inner_val = delegateIterResultValue(frame.ctx, r.value);
      af->delegate_iterator = nullptr;
      af->delegate_phase = AsyncDelegatePhase::None;
      stack.push_back(inner_val);
    } else {
      /* inner yield：AsyncGeneratorYield（先 await 产出值再结算请求挂起）。 */
      ConversionResult r2 =
          asyncDelegateYieldValue(frame.ctx, af, gen_v, r.value);
      if (r2.suspended) {
        frame.suspended = true;
        frame.done = true;
        break;
      }
      error = std::move(r2);
      break;
    }
    break;
  } while (false);
  if (error.value == nullptr &&
      (!error.error.empty() || error.thrown != nullptr))
    return std::move(error);
  return ok(nullptr);
}

/* 分发表：按 Opcode 枚举值下标访问（M5.1e-f 栈帧瘦身）。 */
const OpHandler kHandlers[] = {
    op_PushConst,  /* PushConst */
    op_PushBigInt,  /* PushBigInt */
    op_PushUndefined,  /* PushUndefined */
    op_PushNull,  /* PushNull */
    op_PushTrue,  /* PushTrue */
    op_PushFalse,  /* PushFalse */
    op_PushInt,  /* PushInt */
    op_LoadVar,  /* LoadVar */
    op_StoreVar,  /* StoreVar */
    op_LoadGlobal,  /* LoadGlobal */
    op_Pop,  /* Pop */
    op_Dup,  /* Dup */
    op_Add,  /* Add */
    op_Sub,  /* Sub */
    op_Mul,  /* Mul */
    op_Div,  /* Div */
    op_Mod,  /* Mod */
    op_Pow,  /* Pow */
    op_Neg,  /* Neg */
    op_Not,  /* Not */
    op_Typeof,  /* Typeof */
    op_Eq,  /* Eq */
    op_StrictEq,  /* StrictEq */
    op_Lt,  /* Lt */
    op_Le,  /* Le */
    op_Gt,  /* Gt */
    op_Ge,  /* Ge */
    op_Ne,  /* Ne */
    op_StrictNe,  /* StrictNe */
    op_BitAnd,  /* BitAnd */
    op_BitOr,  /* BitOr */
    op_BitXor,  /* BitXor */
    op_LShift,  /* LShift */
    op_RShift,  /* RShift */
    op_URShift,  /* URShift */
    op_Jump,  /* Jump */
    op_JumpIfFalse,  /* JumpIfFalse */
    op_Call,  /* Call */
    op_CallMethod,  /* CallMethod */
    op_Construct,  /* Construct */
    op_Ret,  /* Ret */
    op_MakeFunction,  /* MakeFunction */
    op_LoadUpvalue,  /* LoadUpvalue */
    op_StoreUpvalue,  /* StoreUpvalue */
    op_NewObject,  /* NewObject */
    op_NewArray,  /* NewArray */
    op_LoadProp,  /* LoadProp */
    op_StoreProp,  /* StoreProp */
    op_LoadElem,  /* LoadElem */
    op_StoreElem,  /* StoreElem */
    op_Print,  /* Print */
    op_Nop,  /* Nop */
    op_GetIterator,  /* GetIterator */
    op_IteratorStep,  /* IteratorStep */
    op_JumpIfTrue,  /* JumpIfTrue */
    op_JumpIfUndefined,  /* JumpIfUndefined */
    op_RestToArray,  /* RestToArray */
    op_RestParam,  /* RestParam */
    op_AppendElem,  /* AppendElem */
    op_AppendSpread,  /* AppendSpread */
    op_CallArray,  /* CallArray */
    op_ConstructArray,  /* ConstructArray */
    op_CopyObjectProps,  /* CopyObjectProps */
    op_JumpIfNullish,  /* JumpIfNullish */
    op_DefineProp,  /* DefineProp */
    op_DefineAccessor,  /* DefineAccessor */
    op_LoadThis,  /* LoadThis */
    op_SetPrototype,  /* SetPrototype */
    op_SetHomeObject,  /* SetHomeObject */
    op_SuperLoadProp,  /* SuperLoadProp */
    op_SuperLoadElem,  /* SuperLoadElem */
    op_SuperCall,  /* SuperCall */
    op_LoadPrivate,  /* LoadPrivate */
    op_StorePrivate,  /* StorePrivate */
    op_HasPrivate,  /* HasPrivate */
    op_InitPrivate,  /* InitPrivate */
    op_SuperCallArray,  /* SuperCallArray */
    op_In,  /* In */
    op_Instanceof,  /* Instanceof */
    op_ToString,  /* ToString */
    op_ToNumber,  /* ToNumber */
    op_BitNot,  /* BitNot */
    op_DeleteProp,  /* DeleteProp */
    op_DeleteElem,  /* DeleteElem */
    op_Increment,  /* Increment */
    op_EnumerateKeys,  /* EnumerateKeys */
    op_NextKey,  /* NextKey */
    op_RestObjectProps,  /* RestObjectProps */
    op_Throw,  /* Throw */
    op_RethrowPending,  /* RethrowPending */
    op_StoreGlobal,  /* StoreGlobal */
    op_CallMethodArray,  /* CallMethodArray */
    op_LoadImport,  /* LoadImport */
    op_StoreImport,  /* StoreImport */
    op_LoadImportMeta,  /* LoadImportMeta */
    op_LoadImportNamespace,  /* LoadImportNamespace */
    op_Await,  /* Await */
    op_Yield,  /* Yield */
    op_YieldDelegate,  /* YieldDelegate */
    op_IteratorClose,  /* IteratorClose */
    op_GetAsyncIterator,  /* GetAsyncIterator */
    op_AsyncIteratorNext,  /* AsyncIteratorNext */
    op_AsyncIteratorClose,  /* AsyncIteratorClose */
    op_IteratorResultToValueDone,  /* IteratorResultToValueDone */
    op_AsyncYieldDelegate,  /* AsyncYieldDelegate */
};

}  // namespace

/* -------------------------------------------------------------------------- */
/* 执行入口                                                                    */
/* -------------------------------------------------------------------------- */

ConversionResult runBytecode(JSContext *ctx, const FunctionBytecode &fn,
                             JSValue *thisVal, JSValue *const *args,
                             size_t argc, JSEnvironment *env,
                             JSFunction *self, size_t start_pc,
                             std::vector<JSValue *> *restore_stack,
                             JSValue *initial_thrown,
                             JSValue *initial_return) {
  /* 操作数栈：栈底为局部变量槽（numLocals 个），之上为表达式求值栈。
   * M5.4 async 恢复：restore_stack 非空时直接采用挂起帧的栈副本
   * （含局部槽 + 表达式中间值），跳过局部槽初始化。 */
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

  if (restore_stack != nullptr) {
    stack = std::move(*restore_stack); /* 采用挂起帧的栈副本 */
  } else {
    size_t args_to_copy = argc < fn.numLocals ? argc : fn.numLocals;
    for (size_t i = 0; i < fn.numLocals; ++i) {
      if (i < args_to_copy && args[i] != nullptr) {
        stack.push_back(args[i]);
      } else {
        stack.push_back(ctx->createValue(Kind::Undefined)); /* 局部槽占位 */
      }
    }
  }

  /* 当前帧：pc + 栈引用。v1 单帧（顶层程序），函数调用经 E3 Call 递归。 */
  size_t pc = start_pc;
  const std::vector<Instruction> &code = fn.code;

  /* 顶层入口的 this 绑定（M5.1c）：函数调用时 thisVal 由 call() 传入
   * （setter/getter 的 this = 目标对象）；顶层（JS_Eval 直入）为 globalThis。
   * 由 call() 的临时句柄或全局根保护；LoadThis 压入栈后受根集保护。 */
  JSValue *frame_this = thisVal;
  if (frame_this == nullptr) frame_this = ctx->globalObject();

  /* 注意：指令执行期间操作数保持栈上（不提前 pop），运算完成后才收缩——
   * 否则操作数弹出后不受根集保护，运算内部分配触发 GC 会被回收（悬垂）。 */

  /* M5.1e-f：异常传播（return 路径，不用 C++ 异常）。
   * 帧级 finally 挂起状态（pending/has_pending/pending_handle）由 VmFrame
   * 持有（见 VmFrame 定义）。主循环经全局分发表 kHandlers 按下标调用 handler，
   * 异常经 handler 返回值（ConversionResult）逐帧传播：tryDispatch 查异常表
   * （命中 -> 恢复栈+跳 handler，继续主循环）；未命中 -> return error 逐帧
   * 返回，跨帧由上层 Call 指令检查传播。不占 C++ 异常展开栈空间，深层递归
   * 安全。
   * 注意：error 每轮由 handler 返回（成功为 ok(nullptr)，value=nullptr 但
   * error 空、thrown=nullptr 不代表失败）；只有显式设置 error.error 或
   * error.thrown 才触发异常分支。 */

  VmFrame f;
  f.ctx = ctx;
  f.fn = &fn;
  f.thisVal = thisVal;
  f.args = args;
  f.argc = argc;
  f.env = env;
  f.self = self;
  f.stack = &stack;
  f.pc = &pc;
  f.frame_this = &frame_this;
  /* M5.4 await reject 续延：启动即注入 JS 异常（rejection 值作为 thrown），
   * 从挂起点后第一条指令进入异常派发——tryDispatch 命中 try/catch 则恢复，
   * 否则向上传播（resumeAsyncFrame 据此 reject 外层 promise）。 */
  if (initial_thrown != nullptr) {
    ConversionResult thrown_err;
    thrown_err.thrown = initial_thrown;
    if (tryDispatch(f.ctx, *f.fn, *f.pc, *f.stack, thrown_err, f.pc,
                    &f.pending, &f.has_pending, &f.pending_handle, f.env)) {
      /* 命中 try/catch：恢复栈+跳 handler，继续主循环执行 catch 体。 */
    } else {
      /* 未命中：释放可能的 pending 句柄，向上传播。 */
      if (f.pending_handle != 0) {
        f.ctx->handles().free(f.pending_handle);
        f.pending_handle = 0;
      }
      return thrown_err;
    }
  }
  /* M5.5b：generator.return() 注入 return completion（§27.5.3.3 step 5-7）。
   * 从 yield 挂起点续延时，把 return value 当作 return completion 派发——
   * tryDispatch 以 is_return=true 找覆盖的 finally，命中则挂起 return
   * completion 跳 finally（finally 执行后 RethrowPending 用 return value 完成）；
   * 未命中 finally 则直接以 return value 完成本帧（压栈 + done）。 */
  if (initial_return != nullptr) {
    ConversionResult ret_comp;
    ret_comp.thrown = initial_return;
    size_t new_pc = 0;
    bool dispatched = tryDispatch(f.ctx, *f.fn, *f.pc, *f.stack, ret_comp,
                                  &new_pc, &f.pending, &f.has_pending,
                                  &f.pending_handle, f.env,
                                  /*is_return=*/true, &f.pending_is_return);
    if (dispatched) {
      *f.pc = new_pc; /* 命中 finally：跳 finally_start，主循环执行 finally */
    } else {
      /* 无 finally 覆盖：直接以 return value 完成本帧。
       * return value 压栈（runBytecode 据栈顶取返回值），直接返回——
       * 不进主循环（避免执行 yield 后续指令如下一条 Pop 栈下溢）。
       * 返回 ok(initial_return) 而非 ok(nullptr)：ConversionResult::ok() 据
       * value!=nullptr 判定，调用方（generator.cc mode==2）据此区分成功完成
       * 与 finally 抛错透传。 */
      stack.push_back(initial_return);
      return ok(initial_return);
    }
  }
  while (*f.pc < f.code().size()) {
    const Instruction &instr = f.code()[*f.pc];
    f.arg = instr.arg;
    f.op = instr.op;
    ++(*f.pc);
    ConversionResult error = kHandlers[static_cast<size_t>(instr.op)](f);
    if (f.done) break; /* Ret 执行：立即结束本帧（返回栈顶结果） */
    if (error.value == nullptr &&
        (!error.error.empty() || error.thrown != nullptr)) {
      if (tryDispatch(f.ctx, *f.fn, *f.pc, *f.stack, error, f.pc,
                      &f.pending, &f.has_pending, &f.pending_handle,
                      f.env)) {
        continue;
      }
      if (f.pending_handle != 0) {
        f.ctx->handles().free(f.pending_handle);
        f.pending_handle = 0;
      }
      return error;
    }
  }
  /* M5.4 async：await 挂起——op_Await 置 f.suspended 后主循环结束，
   * 返回 suspended=true（现场已转移到堆上 AsyncFrame，非错误）。 */
  if (f.suspended) {
    ConversionResult susp;
    susp.suspended = true;
    return susp;
  }
  /* 程序自然结束（无 Ret）：栈顶为结果；空栈 -> undefined。 */
  if (stack.empty()) return ok(ctx->createValue(Kind::Undefined));
  return ok(stack.back());
}

}  // namespace neojs
