/*
 * 内部 C++ 实现：generator 运行时（M5.5a-3，§27.5）。
 *
 * 复用 AsyncFrame 作通用挂起帧（用户确认设计：Generator 内嵌 AsyncFrame 指针）。
 * 详见 include/neojs/runtime/generator.h 的设计说明。
 */

#include "neojs/runtime/generator.h"

#include <utility>
#include <vector>

#include "neojs/runtime/async.h"
#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/iterator.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/value.h"
#include "neojs/runtime/vm.h"

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

/* 创建 {value, done} 迭代器结果对象（Generator.prototype.next/throw/return 返回）。 */
JSValue *createIterResult(JSContext *ctx, JSValue *value, bool done) {
  JSValue *r = ctx->createObject(objectProto(ctx)->object());
  r->object()->setProperty("value",
                           value != nullptr ? value
                                            : ctx->createValue(Kind::Undefined));
  r->object()->setProperty("done", ctx->createValue(done));
  return r;
}

/* 创建 Generator 对象（kind=Generator）+ 包装 JSValue，原型链挂 Generator.prototype。 */
JSValue *createGeneratorObject(JSContext *ctx) {
  JSObject *proto = nullptr;
  JSValue *gp = generatorProto(ctx); /* builtins 注册的 Generator.prototype */
  if (gp != nullptr) proto = gp->object();
  JSObject *obj = ctx->heap().allocGCObject<JSObject>(ObjectKind::Generator, 0);
  if (proto != nullptr) obj->setPrototype(proto);
  ctx->heap().pin(obj); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(obj);
  obj->setHolder(v);
  ctx->heap().unpin(obj);
  return v;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* generator 调用入口                                                         */
/* -------------------------------------------------------------------------- */

ConversionResult generatorCall(JSContext *ctx, JSFunction *fn, JSValue *thisVal,
                               JSValue *const *args, size_t argc) {
  if (fn == nullptr) return fail("Error: null generator function");

  /* 创建 Generator 对象（state=SuspendedStart）。 */
  JSValue *gen_v = createGeneratorObject(ctx);
  ctx->heap().pin(gen_v); /* 分配保护：建帧 + 挂现场前防误回收 */

  /* 创建挂起帧（AsyncFrame，复用 async 现场保存机制）：pc=0，空栈。
   * 首次 next() 时从这里重建现场开始执行（GeneratorStart）。 */
  AsyncFrame *af = ctx->heap().allocGCObject<AsyncFrame>();
  JSValue *afv = wrapAsyncFrame(ctx, af);
  ctx->heap().pin(afv); /* 分配保护：写入字段前防误回收 */

  af->fn = fn;
  af->pc = 0;
  af->stack.clear();
  af->frame_this = thisVal;

  /* 创建帧环境（parent = 函数 [[Environment]]），参数按名写入。
   * 与 asyncCall 一致——首次执行时 runBytecode 用此 env。 */
  JSEnvironment *call_env =
      ctx->heap().allocGCObject<JSEnvironment>(fn->environment());
  ctx->heap().pin(call_env);
  const FunctionBytecode &bc = fn->bytecode();
  size_t nparams = argc < bc.locals.size() ? argc : bc.locals.size();
  for (size_t i = 0; i < nparams; ++i) {
    if (i < argc && args[i] != nullptr && !bc.locals[i].empty()) {
      call_env->setBinding(bc.locals[i], args[i]);
    }
  }
  af->env = call_env;
  af->promise = nullptr;        /* generator 无 promise（async 专用） */
  af->yield_value = nullptr;    /* 首次 next 执行到 yield 才填 */
  af->delegate_iterator = nullptr; /* M5.5a-4：yield* 委托迭代器，委托中才非空 */

  /* 写入隐藏绑定 __generator__（op_Yield 沿 env 链查找当前 generator）。
   * 与 asyncCall 的 __async_promise__ 同模式。 */
  call_env->setBinding(kGeneratorBinding, gen_v);

  ctx->heap().unpin(call_env);

  /* 挂现场到 Generator（[[GeneratorContext]]）。 */
  gen_v->object()->setGeneratorContext(afv);
  gen_v->object()->setGeneratorState(
      static_cast<int>(GeneratorState::SuspendedStart));

  ctx->heap().unpin(afv);
  ctx->heap().unpin(gen_v);
  return ok(gen_v);
}

/* -------------------------------------------------------------------------- */
/* 恢复 Generator 执行                                                         */
/* -------------------------------------------------------------------------- */

ConversionResult resumeGenerator(JSContext *ctx, JSValue *gen_v,
                                 JSValue *value, int mode) {
  /* mode: 0 next / 1 throw / 2 return */
  if (gen_v == nullptr || !gen_v->isObject() ||
      !gen_v->object()->isGenerator()) {
    return fail("TypeError: not a generator");
  }
  JSObject *gen = gen_v->object();
  int state = gen->generatorState();

  /* completed：next/throw/return 都返回 {value, done:true}（throw 抛出 value，
   * §27.5.3.3 GeneratorResumeAbrupt step 8）。简化：completed 下 next/return
   * 返回 {value:undefined|value, done:true}；throw 直接抛 value。 */
  if (state == static_cast<int>(GeneratorState::Completed)) {
    if (mode == 1) {
      /* throw on completed：抛出 value（§27.5.3.3 step 8.b）。 */
      ConversionResult r;
      r.thrown = (value != nullptr) ? value
                                    : ctx->createValue(Kind::Undefined);
      return r;
    }
    JSValue *v =
        (mode == 2 && value != nullptr) ? value : ctx->createValue(Kind::Undefined);
    return ok(createIterResult(ctx, v, true));
  }

  /* running：重入禁止（§27.5.3.1 step 2 / §27.5.3.3 step 2）。
   * next/throw/return 在 running 状态调用抛 TypeError。 */
  if (state == static_cast<int>(GeneratorState::Running)) {
    return fail("TypeError: Generator is already running");
  }

  /* SuspendedStart / SuspendedYield：恢复执行。 */
  JSValue *afv = gen->generatorContext();
  AsyncFrame *frame = asAsyncFrame(afv);
  if (frame == nullptr || frame->fn == nullptr) {
    return fail("Error: invalid generator context");
  }

  /* M5.5a-4 yield* 委托恢复：frame.delegate_iterator 非空表示当前挂在
   * yield* 上，next/throw/return 转发给 inner iterator（而非压栈/注入外层）。
   * 委托路径内部处理 state 翻转（Running→SuspendedYield/Completed）。 */
  if (frame->delegate_iterator != nullptr) {
    gen->setGeneratorState(static_cast<int>(GeneratorState::Running));
    return yieldDelegateResume(ctx, gen_v, value, mode);
  }

  gen->setGeneratorState(static_cast<int>(GeneratorState::Running));

  /* return 模式：强制完成——置 completed，返回 {value, done:true}。
   * 简化：未实现 try/finally 穿透与 finally 块恢复（§27.5.3.3 step 5-7），
   * M5.5a-3 阶段直接完成。 */
  if (mode == 2) {
    gen->setGeneratorState(static_cast<int>(GeneratorState::Completed));
    gen->setGeneratorContext(nullptr);
    JSValue *v = (value != nullptr) ? value : ctx->createValue(Kind::Undefined);
    return ok(createIterResult(ctx, v, true));
  }

  /* next/throw：从 frame 重建现场续延。
   *   next：value 压栈（作为 yield 表达式的值）
   *   throw：value 作异常注入（yield 处抛出，可被 try/catch 捕获）
   * 首次 next（SuspendedStart）：pc=0，无需压栈（首条指令即函数体开头）；
   *   value 应为 undefined（规范：首参被忽略，GeneratorStart 不接收值）。 */
  std::vector<JSValue *> restore_stack = std::move(frame->stack);
  JSValue *thrown = nullptr;
  const bool is_first_resume =
      (state == static_cast<int>(GeneratorState::SuspendedStart));
  if (mode == 0) {
    /* next：非首次压栈 value（首次忽略 value，压 undefined 保持栈平衡） */
    if (is_first_resume) {
      restore_stack.push_back(ctx->createValue(Kind::Undefined));
    } else {
      restore_stack.push_back(value != nullptr ? value
                                               : ctx->createValue(Kind::Undefined));
    }
  } else {
    /* throw：异常注入 */
    thrown = (value != nullptr) ? value : ctx->createValue(Kind::Undefined);
  }

  /* 续延执行：从 frame->pc 继续到下个 yield 或函数完成。 */
  ConversionResult r = runBytecode(
      ctx, frame->fn->bytecode(), frame->frame_this, nullptr, 0, frame->env,
      frame->fn, frame->pc, &restore_stack, thrown);

  if (r.suspended) {
    /* 又挂起（新现场已存 frame，op_Yield 已写 yield_value）。 */
    JSValue *yv = frame->yield_value;
    if (yv == nullptr) yv = ctx->createValue(Kind::Undefined);
    gen->setGeneratorState(static_cast<int>(GeneratorState::SuspendedYield));
    return ok(createIterResult(ctx, yv, false));
  }

  /* 函数完成（正常返回或抛错）。 */
  gen->setGeneratorContext(nullptr);
  gen->setGeneratorState(static_cast<int>(GeneratorState::Completed));

  if (!r.ok()) {
    /* 执行中抛错：向 next/throw 调用方传播（GeneratorResume/ResumeAbrupt
     * 的异常透传给调用方）。 */
    return r;
  }

  /* 正常完成：返回 {value:retval, done:true}。 */
  JSValue *retval = r.value;
  if (retval == nullptr) retval = ctx->createValue(Kind::Undefined);
  return ok(createIterResult(ctx, retval, true));
}

/* -------------------------------------------------------------------------- */
/* M5.5a-4 yield* 委托                                                        */
/* -------------------------------------------------------------------------- */

/* 取 AsyncFrame（gen_v 内嵌的挂起帧）。 */
static AsyncFrame *genFrame(JSValue *gen_v) {
  if (gen_v == nullptr || !gen_v->isObject() ||
      !gen_v->object()->isGenerator()) {
    return nullptr;
  }
  return asAsyncFrame(gen_v->object()->generatorContext());
}

/* 读迭代结果的 done 属性（true/false）。无 done 属性视作 false（继续迭代）。 */
static bool iterResultDone(JSValue *iter_result) {
  if (iter_result == nullptr || !iter_result->isObject()) return true;
  JSValue *d = iter_result->object()->getProperty("done");
  return (d != nullptr) && toBoolean(d);
}

/* 读迭代结果的 value 属性（缺省 undefined）。 */
static JSValue *iterResultValue(JSContext *ctx, JSValue *iter_result) {
  if (iter_result == nullptr || !iter_result->isObject()) {
    return ctx->createValue(Kind::Undefined);
  }
  JSValue *v = iter_result->object()->getProperty("value");
  return (v != nullptr) ? v : ctx->createValue(Kind::Undefined);
}

/* 取 inner iterator 的 throw/return 方法（§7.4.6 IteratorReturn /
 * yield* 委托 throw 语义）。无方法返回 nullptr（非错误）。 */
static JSValue *getIteratorMethod(JSContext *ctx, JSValue *iterator,
                                  const char *name) {
  if (iterator == nullptr || !iterator->isObject()) return nullptr;
  JSValue *m = iterator->object()->getProperty(name);
  if (m == nullptr || m->kind() == Kind::Undefined ||
      m->kind() == Kind::Null) {
    return nullptr;
  }
  if (!isCallable(m)) return nullptr;
  return m;
}

/* 调 inner.next() -> iter_result。失败透传（thrown/error）。 */
static ConversionResult callInnerNext(JSContext *ctx, JSValue *iterator) {
  return iteratorNext(ctx, iterator);
}

/* 续延外层字节码（前向声明：yieldDelegateResume 调用，定义在 yieldDelegateResume 之后）。
 * inner done 后压 inner.value 续延外层，或注入 thrown 续延外层异常派发。 */
static ConversionResult resumeOuterBytecode(JSContext *ctx, JSValue *gen_v,
                                            AsyncFrame *frame,
                                            JSValue *inner_value,
                                            JSValue *thrown);

/* 调 inner.throw(exception) 或 inner.return(value)。
 *   method   iterator.throw / iterator.return（已校验可调用）
 *   value    透传给方法的参数
 * 返回 iter_result（必须是对象，否则 TypeError），失败透传。 */
static ConversionResult callInnerMethod(JSContext *ctx, JSValue *iterator,
                                        JSValue *method, JSValue *value) {
  JSValue *argv[1] = {
      (value != nullptr) ? value : ctx->createValue(Kind::Undefined)};
  ConversionResult r =
      call(ctx, method, iterator, argv, 1);
  if (!r.ok()) return r;
  if (!r.value->isObject()) {
    return fail("TypeError: iterator result is not an object");
  }
  return r;
}

/* yield* 委托启动（op_YieldDelegate 调用，§14.4 / §27.5.3.3）。
 * 流程（§27.5.3.2 GeneratorYield 拆 outter + inner）：
 *   1. getIterator(iterable) -> inner，存 frame.delegate_iterator
 *   2. inner.next() 首步：
 *      - inner done -> 取 inner.value，清 delegate_iterator，把 inner.value
 *        压回外层 frame.stack（yield* 表达式的结果），返回 ok（非挂起，
 *        op_YieldDelegate 据此让外层字节码继续执行）；
 *      - inner yield（done=false）-> frame.yield_value = inner.value，置
 *        frame.suspended（delegate_iterator 保留），返回 suspended=true
 *        （op_YieldDelegate 结束本帧 runBytecode，把 {value,done:false} 返
 *        回给外层 next() 调用方）；
 *      - inner 抛错 -> 透传 thrown/error（外层 try/catch 可捕）。 */
ConversionResult yieldDelegateStart(JSContext *ctx, JSValue *gen_v,
                                    JSValue *iterable) {
  AsyncFrame *frame = genFrame(gen_v);
  if (frame == nullptr) {
    return fail("Error: yield* outside of generator function");
  }

  /* 1. getIterator(iterable)。 */
  ConversionResult it = getIterator(ctx, iterable);
  if (!it.ok()) return it;
  ctx->heap().pin(it.value); /* 分配保护：写 frame 前防误回收 */
  frame->delegate_iterator = it.value;
  ctx->heap().unpin(it.value);

  /* 2. inner.next() 首步。 */
  ConversionResult r = callInnerNext(ctx, frame->delegate_iterator);
  if (!r.ok()) {
    /* inner 抛错：清 delegate，透传错误（外层 try/catch 可捕）。 */
    frame->delegate_iterator = nullptr;
    return r;
  }
  JSValue *iter_result = r.value;

  if (iterResultDone(iter_result)) {
    /* inner 已 done：取 inner.value 压回外层栈，清 delegate。
     * yield* 表达式结果 = inner.value，外层字节码继续执行。 */
    JSValue *inner_val = iterResultValue(ctx, iter_result);
    frame->stack.push_back(inner_val);
    frame->delegate_iterator = nullptr;
    /* 非挂起：op_YieldDelegate 把栈顶值作为 yield* 结果继续外层执行。 */
    ConversionResult ret;
    ret.value = nullptr; /* nullptr 表示非挂起、外层续延（op_YieldDelegate 约定） */
    ret.suspended = false;
    return ret;
  }

  /* inner yield（done=false）：置 yield_value，挂起外层 generator。
   * delegate_iterator 保留，下次 next/throw/return 转发给 inner。 */
  frame->yield_value = iterResultValue(ctx, iter_result);
  ConversionResult ret;
  ret.value = nullptr;
  ret.suspended = true; /* 信号：op_YieldDelegate 置 frame.suspended 结束本帧 */
  return ret;
}

/* yield* 委托恢复（resumeGenerator 检测 frame.delegate_iterator 非空时调用，
 * 替代普通压栈/异常注入路径，§27.5.3.3 GeneratorResumeAbrupt step 6-7）。
 *   mode 0 next  -> inner.next(value)
 *   mode 1 throw -> inner.throw(value)（无 throw 方法→透传 value 抛出外层）
 *   mode 2 return-> inner.return(value)（无 return 方法→直接完成外层 generator）
 * inner 响应：
 *   - inner done -> 清 delegate_iterator，压 inner.value 到 frame.stack，
 *     续延外层字节码（runBytecode from frame->pc，可能再次挂起或完成）；
 *   - inner yield（done=false）-> frame.yield_value = inner.value，置
 *     SuspendedYield，返回 {value,done:false}；
 *   - inner 抛错 -> 透传 thrown/error（外层 try/catch 可捕或向上传播）。 */
ConversionResult yieldDelegateResume(JSContext *ctx, JSValue *gen_v,
                                     JSValue *value, int mode) {
  AsyncFrame *frame = genFrame(gen_v);
  if (frame == nullptr || frame->delegate_iterator == nullptr) {
    return fail("Error: yield* delegate resume with no inner iterator");
  }
  JSValue *iterator = frame->delegate_iterator;
  JSObject *gen = gen_v->object();

  /* mode 2 return + inner 无 return 方法：直接完成外层 generator
   * （§27.5.3.3 step 7.c：CloseIterator(inner) 后置 completed 返回 {value,done:true}）。
   * 简化：未调 inner.return（无方法），直接完成。 */
  if (mode == 2) {
    JSValue *ret_method = getIteratorMethod(ctx, iterator, "return");
    if (ret_method == nullptr) {
      /* 无 return 方法：直接完成外层 generator。 */
      frame->delegate_iterator = nullptr;
      gen->setGeneratorContext(nullptr);
      gen->setGeneratorState(static_cast<int>(GeneratorState::Completed));
      JSValue *v =
          (value != nullptr) ? value : ctx->createValue(Kind::Undefined);
      return ok(createIterResult(ctx, v, true));
    }
    /* 有 return 方法：转发，按返回结果 done 分支处理。 */
    ConversionResult r = callInnerMethod(ctx, iterator, ret_method, value);
    if (!r.ok()) {
      /* inner.return 抛错：清 delegate，透传错误（外层 try/catch 可捕）。 */
      frame->delegate_iterator = nullptr;
      return r;
    }
    JSValue *iter_result = r.value;
    if (iterResultDone(iter_result)) {
      /* inner.return 返回 done=true：完成外层 generator，返回 {value,done:true}。 */
      JSValue *inner_val = iterResultValue(ctx, iter_result);
      frame->delegate_iterator = nullptr;
      gen->setGeneratorContext(nullptr);
      gen->setGeneratorState(static_cast<int>(GeneratorState::Completed));
      return ok(createIterResult(ctx, inner_val, true));
    }
    /* inner.return 返回 done=false：罕见（inner 捕获 return 继续迭代）。
     * 置 yield_value，挂起外层，返回 {value,done:false}。 */
    frame->yield_value = iterResultValue(ctx, iter_result);
    gen->setGeneratorState(static_cast<int>(GeneratorState::SuspendedYield));
    return ok(createIterResult(ctx, frame->yield_value, false));
  }

  /* mode 1 throw：inner.throw(value)。
   *   无 throw 方法→透传 value 抛出外层（§27.5.3.3 step 6.c：CloseIterator
   *   后 throw value，外层 try/catch 可捕）。 */
  if (mode == 1) {
    JSValue *throw_method = getIteratorMethod(ctx, iterator, "throw");
    if (throw_method == nullptr) {
      /* 无 throw 方法：清 delegate，把 value 当异常抛给外层 generator。
       * 续延外层字节码（initial_thrown=value，try/catch 可捕）。 */
      frame->delegate_iterator = nullptr;
      JSValue *thrown =
          (value != nullptr) ? value : ctx->createValue(Kind::Undefined);
      return resumeOuterBytecode(ctx, gen_v, frame, nullptr, thrown);
    }
    ConversionResult r = callInnerMethod(ctx, iterator, throw_method, value);
    if (!r.ok()) {
      /* inner.throw 抛错：清 delegate，透传错误。 */
      frame->delegate_iterator = nullptr;
      return r;
    }
    JSValue *iter_result = r.value;
    if (iterResultDone(iter_result)) {
      /* inner.throw 返回 done=true：清 delegate，压 inner.value 到外层栈，
       * 续延外层字节码（yield* 表达式结果 = inner.value）。 */
      JSValue *inner_val = iterResultValue(ctx, iter_result);
      frame->delegate_iterator = nullptr;
      return resumeOuterBytecode(ctx, gen_v, frame, inner_val, nullptr);
    }
    /* inner.throw 返回 done=false：置 yield_value，挂起外层。 */
    frame->yield_value = iterResultValue(ctx, iter_result);
    gen->setGeneratorState(static_cast<int>(GeneratorState::SuspendedYield));
    return ok(createIterResult(ctx, frame->yield_value, false));
  }

  /* mode 0 next：inner.next(value)。 */
  {
    /* iteratorNext 不带参，需要带参版本：直接 call iterator.next。 */
    JSValue *next_method = nullptr;
    if (iterator->isObject()) {
      next_method = iterator->object()->getProperty("next");
    }
    if (next_method == nullptr || !isCallable(next_method)) {
      frame->delegate_iterator = nullptr;
      return fail("TypeError: iterator.next is not a function");
    }
    ConversionResult r = callInnerMethod(ctx, iterator, next_method, value);
    if (!r.ok()) {
      frame->delegate_iterator = nullptr;
      return r;
    }
    JSValue *iter_result = r.value;
    if (iterResultDone(iter_result)) {
      /* inner done：清 delegate，压 inner.value 到外层栈，续延外层字节码。 */
      JSValue *inner_val = iterResultValue(ctx, iter_result);
      frame->delegate_iterator = nullptr;
      return resumeOuterBytecode(ctx, gen_v, frame, inner_val, nullptr);
    }
    /* inner yield：置 yield_value，挂起外层。 */
    frame->yield_value = iterResultValue(ctx, iter_result);
    gen->setGeneratorState(static_cast<int>(GeneratorState::SuspendedYield));
    return ok(createIterResult(ctx, frame->yield_value, false));
  }
}

/* 续延外层字节码：从 frame->pc 继续，restore_stack + 压入 inner_value
 * （非 nullptr 时）或注入 thrown（非 nullptr 时）。inner done 后的路径。
 * 结果处理与 resumeGenerator 续延段一致（suspended→{value,done:false}，
 * 完成→{value,done:true}，抛错→透传）。 */
static ConversionResult resumeOuterBytecode(JSContext *ctx, JSValue *gen_v,
                                            AsyncFrame *frame,
                                            JSValue *inner_value,
                                            JSValue *thrown) {
  JSObject *gen = gen_v->object();
  std::vector<JSValue *> restore_stack = std::move(frame->stack);
  if (inner_value != nullptr) {
    restore_stack.push_back(inner_value);
  }
  ConversionResult r = runBytecode(
      ctx, frame->fn->bytecode(), frame->frame_this, nullptr, 0, frame->env,
      frame->fn, frame->pc, &restore_stack, thrown);

  if (r.suspended) {
    /* 外层又挂起（op_Yield 或 op_YieldDelegate）。yield_value 已写入 frame。 */
    JSValue *yv = frame->yield_value;
    if (yv == nullptr) yv = ctx->createValue(Kind::Undefined);
    gen->setGeneratorState(static_cast<int>(GeneratorState::SuspendedYield));
    return ok(createIterResult(ctx, yv, false));
  }

  /* 外层完成（正常返回或抛错）。 */
  gen->setGeneratorContext(nullptr);
  gen->setGeneratorState(static_cast<int>(GeneratorState::Completed));

  if (!r.ok()) {
    /* 执行中抛错：透传给 next/throw 调用方。 */
    return r;
  }

  JSValue *retval = r.value;
  if (retval == nullptr) retval = ctx->createValue(Kind::Undefined);
  return ok(createIterResult(ctx, retval, true));
}

/* -------------------------------------------------------------------------- */
/* Generator.prototype.next / throw / return                                  */
/* -------------------------------------------------------------------------- */

ConversionResult generatorProtoNext(JSContext *ctx, JSValue *self,
                                     JSValue *const *args, size_t argc,
                                     int mode) {
  JSValue *v = (argc > 0 && args[0] != nullptr)
                   ? args[0]
                   : ctx->createValue(Kind::Undefined);
  ConversionResult r = resumeGenerator(ctx, self, v, mode);
  if (!r.ok()) {
    /* throw 路径：resumeGenerator 返回 thrown（JS throw 值）或 error（内部
     * 错误串）。直接透传给 call() Builtin 路径——op_Call 检测 r.ok()==false
     * 走异常传播（tryDispatch 命中 try/catch 则恢复，否则向上抛）。
     *   - thrown != nullptr：JS throw 值（如 completed 状态 throw、yield 处
     *     抛出且未被捕），保持原值透传
     *   - thrown == nullptr + error：内部错误串，op_Throw/tryDispatch 会包装
     *     为 Kind::Exception 值（§14.16.3 步骤 5-7）
     * 此处不再用 Kind::Exception 包装——保持与 op_Throw 一致的 thrown 语义。 */
    return r;
  }
  return ok(r.value);
}

/* -------------------------------------------------------------------------- */
/* yield 挂起（op_Yield 调用）                                                  */
/* -------------------------------------------------------------------------- */

bool suspendYield(JSContext *ctx, JSValue *gen_v, JSFunction *fn,
                  size_t next_pc, std::vector<JSValue *> &stack,
                  JSEnvironment *env, JSValue *frame_this,
                  const ConversionResult &pending, bool has_pending,
                  uint32_t pending_handle, JSValue *yield_val) {
  if (gen_v == nullptr || !gen_v->isObject() ||
      !gen_v->object()->isGenerator()) {
    return false;
  }
  JSObject *gen = gen_v->object();
  JSValue *afv = gen->generatorContext();
  AsyncFrame *frame = asAsyncFrame(afv);
  if (frame == nullptr) {
    return false;
  }

  /* 保存现场到现有 AsyncFrame（复用 generatorCall 创建的同一帧对象）。
   * 与 suspendAwait 不同：不创建新帧，直接更新现有帧字段。 */
  frame->fn = fn;
  frame->pc = next_pc;
  frame->stack = stack; /* 拷贝操作数栈（yield 值已弹栈） */
  frame->env = env;
  frame->frame_this = frame_this;
  frame->pending = pending;
  frame->has_pending = has_pending;
  frame->pending_handle = pending_handle;
  frame->yield_value = yield_val; /* yield 值，resumeGenerator 取出包装 */

  /* generator 挂起是同步的：不入队微任务，直接返回给 next 调用方。
   * 调用方（runBytecode）置 frame.suspended 结束本帧。 */
  (void)ctx;
  return true;
}

}  // namespace neojs
