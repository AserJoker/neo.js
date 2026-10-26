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
