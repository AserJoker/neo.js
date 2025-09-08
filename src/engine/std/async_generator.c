#include "engine/std/async_generator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "engine/basetype/callable.h"
#include "engine/basetype/coroutine.h"
#include "engine/basetype/error.h"
#include "engine/basetype/interrupt.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include "runtime/vm.h"
#include <stdbool.h>
#include <string.h>

neo_js_variable_t neo_js_async_generator_iterator(neo_js_context_t ctx,
                                                  neo_js_variable_t self,
                                                  uint32_t argc,
                                                  neo_js_variable_t *argv) {
  return self;
}
neo_js_variable_t neo_js_async_generator_constructor(neo_js_context_t ctx,
                                                     neo_js_variable_t self,
                                                     uint32_t argc,
                                                     neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}
static neo_js_variable_t
neo_js_async_generator_on_yield_fulfilled(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, "#resolve");
  neo_js_variable_t value = NULL;
  if (argc > 0) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_js_context_set_field(ctx, result,
                           neo_js_context_create_string(ctx, "done"),
                           neo_js_context_create_boolean(ctx, false), NULL);
  neo_js_context_set_field(
      ctx, result, neo_js_context_create_string(ctx, "value"), value, NULL);
  return neo_js_context_call(ctx, resolve, resolve, 1, &result);
}

static neo_js_variable_t
neo_js_async_generator_on_await_fulfilled(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, "#task");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_js_scope_t current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
  neo_js_variable_t value = NULL;
  if (argc > 0) {
    value = neo_js_context_create_variable(
        ctx, neo_js_variable_get_chunk(argv[0]), NULL);
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_context_set_scope(ctx, current);
  neo_list_push(co_ctx->vm->stack, value);
  neo_js_context_create_micro_task(
      ctx, task, neo_js_context_create_undefined(ctx), 0, NULL, 0, false);
  return neo_js_context_create_undefined(ctx);
}
static neo_js_variable_t
neo_js_async_generator_on_await_rejected(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, "#task");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_js_scope_t current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
  neo_js_variable_t value = NULL;
  if (argc > 0) {
    value = neo_js_context_create_variable(
        ctx, neo_js_variable_get_chunk(argv[0]), NULL);
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_context_set_scope(ctx, current);
  neo_list_push(co_ctx->vm->stack, value);
  co_ctx->vm->offset = neo_buffer_get_size(co_ctx->program->codes);
  neo_js_context_create_micro_task(
      ctx, task, neo_js_context_create_undefined(ctx), 0, NULL, 0, false);
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t neo_js_async_generator_task(neo_js_context_t ctx,
                                                     neo_js_variable_t self,
                                                     uint32_t argc,
                                                     neo_js_variable_t *argv) {
  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, "#resolve");
  neo_js_variable_t reject = neo_js_context_load_variable(ctx, "#reject");
  neo_js_variable_t on_await_fulfilled =
      neo_js_context_load_variable(ctx, "#onAwaitFulfilled");
  neo_js_variable_t on_await_rejected =
      neo_js_context_load_variable(ctx, "#onAwaitRejected");
  neo_js_variable_t on_yield_fulfilled =
      neo_js_context_load_variable(ctx, "#onYieldFulfilled");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, "#task");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_list_t stacktrace =
      neo_js_context_set_stacktrace(ctx, co_ctx->stacktrace);
  neo_js_context_push_stackframe(ctx, NULL, "_.awaiter", 0, 0);
  neo_js_variable_t value = neo_js_vm_exec(co_ctx->vm, co_ctx->program);
  neo_js_context_pop_stackframe(ctx);
  neo_js_context_set_stacktrace(ctx, stacktrace);
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_INTERRUPT) {
    neo_js_interrupt_t interrupt = neo_js_variable_to_interrupt(value);
    neo_js_scope_t current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
    value = neo_js_context_create_variable(ctx, interrupt->result, NULL);
    neo_js_context_set_scope(ctx, current);
    co_ctx->vm->offset = interrupt->offset;
    if (interrupt->type == NEO_JS_INTERRUPT_AWAIT) {
      if (neo_js_context_is_thenable(ctx, value)) {
        neo_js_variable_t then = neo_js_context_get_field(
            ctx, value, neo_js_context_create_string(ctx, "then"), NULL);
        neo_js_variable_t args[] = {on_await_fulfilled, on_await_rejected};
        neo_js_context_call(ctx, then, value, 2, args);
      } else {
        neo_list_push(co_ctx->vm->stack, value);
        neo_js_context_create_micro_task(
            ctx, task, neo_js_context_create_undefined(ctx), 0, NULL, 0, false);
      }
    } else if (interrupt->type == NEO_JS_INTERRUPT_BACKEND_AWAIT) {
      if (neo_js_context_is_thenable(ctx, value)) {
        neo_js_variable_t then = neo_js_context_get_field(
            ctx, value, neo_js_context_create_string(ctx, "then"), NULL);
        neo_js_variable_t args[] = {task, on_await_rejected};
        neo_js_context_call(ctx, then, value, 2, args);
      } else {
        neo_list_push(co_ctx->vm->stack, value);
        neo_js_context_create_micro_task(
            ctx, task, neo_js_context_create_undefined(ctx), 0, NULL, 0, false);
      }
    } else {
      if (neo_js_context_is_thenable(ctx, value)) {
        neo_js_variable_t then = neo_js_context_get_field(
            ctx, value, neo_js_context_create_string(ctx, "then"), NULL);
        neo_js_variable_t args[] = {on_yield_fulfilled, reject};
        neo_js_context_call(ctx, then, value, 2, args);
      } else {
        neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
        neo_js_context_set_field(
            ctx, result, neo_js_context_create_string(ctx, "done"),
            neo_js_context_create_boolean(ctx, false), NULL);
        neo_js_context_set_field(ctx, result,
                                 neo_js_context_create_string(ctx, "value"),
                                 value, NULL);
        neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx),
                            1, &result);
      }
    }
  } else {
    if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
      value = neo_js_error_get_error(ctx, value);
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &value);
    } else {
      neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, "done"),
                               neo_js_context_create_boolean(ctx, true), NULL);
      neo_js_context_set_field(ctx, resolve,
                               neo_js_context_create_string(ctx, "value"),
                               value, NULL);
      neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                          &result);
    }
    co_ctx->result = neo_js_variable_get_chunk(value);
    neo_js_chunk_add_parent(co_ctx->result,
                            neo_js_variable_get_chunk(coroutine));
    neo_js_context_recycle_coroutine(ctx, coroutine);
  }
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t
neo_js_async_generator_resolver(neo_js_context_t ctx, neo_js_variable_t self,
                                uint32_t argc, neo_js_variable_t *argv) {
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t corotuine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_variable_t task =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_async_generator_task);
  neo_js_variable_t on_yield_fulfilled = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_async_generator_on_yield_fulfilled);
  neo_js_variable_t on_await_fulfilled = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_async_generator_on_await_fulfilled);
  neo_js_variable_t on_await_rejected = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_async_generator_on_await_rejected);

  neo_js_callable_set_closure(ctx, task, "#coroutine", corotuine);
  neo_js_callable_set_closure(ctx, task, "#onYieldFulfilled",
                              on_yield_fulfilled);
  neo_js_callable_set_closure(ctx, task, "#onAwaitFulfilled",
                              on_await_fulfilled);
  neo_js_callable_set_closure(ctx, task, "#onAwaitRejected", on_await_rejected);

  neo_js_callable_set_closure(ctx, task, "#task", task);
  neo_js_callable_set_closure(ctx, task, "#resolve", resolve);
  neo_js_callable_set_closure(ctx, task, "#reject", reject);

  neo_js_callable_set_closure(ctx, on_yield_fulfilled, "#resolve", resolve);

  neo_js_callable_set_closure(ctx, on_await_fulfilled, "#task", task);
  neo_js_callable_set_closure(ctx, on_await_fulfilled, "#coroutine", corotuine);

  neo_js_callable_set_closure(ctx, on_await_rejected, "#task", task);
  neo_js_callable_set_closure(ctx, on_await_rejected, "#coroutine", corotuine);

  neo_js_context_create_micro_task(
      ctx, task, neo_js_context_create_undefined(ctx), 0, NULL, 0, false);
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_async_generator_next(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              uint32_t argc,
                                              neo_js_variable_t *argv) {
  neo_js_variable_t coroutine =
      neo_js_context_get_internal(ctx, self, "[[coroutine]]");
  neo_js_co_context_t co = neo_js_coroutine_get_context(coroutine);
  neo_js_variable_t promise = neo_js_context_get_std(ctx).promise_constructor;
  if (co->result) {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, "done"),
                             neo_js_context_create_boolean(ctx, true), NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, "value"),
        neo_js_context_create_variable(ctx, co->result, NULL), NULL);
    neo_js_variable_t resolve = neo_js_context_get_field(
        ctx, promise, neo_js_context_create_string(ctx, "resolve"), NULL);
    return neo_js_context_call(ctx, resolve, promise, 1, &result);
  }
  neo_js_scope_t current = neo_js_context_set_scope(ctx, co->vm->scope);
  neo_js_variable_t arg = NULL;
  if (argc > 0) {
    arg = neo_js_context_create_variable(
        ctx, neo_js_variable_get_chunk(argv[0]), NULL);
  } else {
    arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_context_set_scope(ctx, current);
  neo_list_push(co->vm->stack, arg);
  neo_js_variable_t resolver = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_async_generator_resolver);
  neo_js_callable_set_closure(ctx, resolver, "#coroutine", coroutine);
  return neo_js_context_construct(ctx, promise, 1, &resolver);
}

neo_js_variable_t neo_js_async_generator_return(neo_js_context_t ctx,
                                                neo_js_variable_t self,
                                                uint32_t argc,
                                                neo_js_variable_t *argv) {
  neo_js_variable_t coroutine =
      neo_js_context_get_internal(ctx, self, "[[coroutine]]");
  neo_js_co_context_t co = neo_js_coroutine_get_context(coroutine);
  neo_js_variable_t promise = neo_js_context_get_std(ctx).promise_constructor;
  if (co->result) {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, "done"),
                             neo_js_context_create_boolean(ctx, true), NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, "value"),
        neo_js_context_create_variable(ctx, co->result, NULL), NULL);
    neo_js_variable_t resolve = neo_js_context_get_field(
        ctx, promise, neo_js_context_create_string(ctx, "resolve"), NULL);
    return neo_js_context_call(ctx, resolve, promise, 1, &result);
  }
  neo_js_scope_t current = neo_js_context_set_scope(ctx, co->vm->scope);
  neo_js_variable_t arg = NULL;
  if (argc > 0) {
    arg = neo_js_context_create_variable(
        ctx, neo_js_variable_get_chunk(argv[0]), NULL);
  } else {
    arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_context_set_scope(ctx, current);
  neo_list_push(co->vm->stack, arg);
  co->vm->offset = neo_buffer_get_size(co->program->codes);
  neo_js_variable_t resolver = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_async_generator_resolver);
  neo_js_callable_set_closure(ctx, resolver, "#coroutine", coroutine);
  return neo_js_context_construct(ctx, promise, 1, &resolver);
}

neo_js_variable_t neo_js_async_generator_throw(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  neo_js_variable_t coroutine =
      neo_js_context_get_internal(ctx, self, "[[coroutine]]");
  neo_js_co_context_t co = neo_js_coroutine_get_context(coroutine);
  neo_js_variable_t promise = neo_js_context_get_std(ctx).promise_constructor;
  if (co->result) {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, "done"),
                             neo_js_context_create_boolean(ctx, true), NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, "value"),
        neo_js_context_create_variable(ctx, co->result, NULL), NULL);
    neo_js_variable_t resolve = neo_js_context_get_field(
        ctx, promise, neo_js_context_create_string(ctx, "resolve"), NULL);
    return neo_js_context_call(ctx, resolve, promise, 1, &result);
  }
  neo_js_scope_t current = neo_js_context_set_scope(ctx, co->vm->scope);
  neo_js_variable_t arg = NULL;
  if (argc > 0) {
    arg = neo_js_context_create_error(ctx, argv[0]);
  } else {
    arg = neo_js_context_create_error(
        ctx, neo_js_context_construct(
                 ctx, neo_js_context_get_std(ctx).error_constructor, 0, NULL));
  }
  neo_js_context_set_scope(ctx, current);
  neo_list_push(co->vm->stack, neo_js_context_create_error(ctx, arg));
  co->vm->offset = neo_buffer_get_size(co->program->codes);
  neo_js_variable_t resolver = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_async_generator_resolver);
  neo_js_callable_set_closure(ctx, resolver, "#coroutine", coroutine);
  return neo_js_context_construct(ctx, promise, 1, &resolver);
}

void neo_js_context_init_std_async_generator(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).async_generator_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL);

  neo_js_variable_t to_string_tag = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "toStringTag"), NULL);

  neo_js_context_def_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, "AsyncGenerator"),
                           true, false, true);

  neo_js_variable_t async_iterator = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "asyncIterator"), NULL);

  neo_js_context_def_field(
      ctx, prototype, async_iterator,
      neo_js_context_create_cfunction(ctx, "[Symbol.asyncIterator]",
                                      neo_js_async_generator_iterator),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "next"),
      neo_js_context_create_cfunction(ctx, "next", neo_js_async_generator_next),
      true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "return"),
                           neo_js_context_create_cfunction(
                               ctx, "return", neo_js_async_generator_return),
                           true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "throw"),
                           neo_js_context_create_cfunction(
                               ctx, "throw", neo_js_async_generator_throw),
                           true, false, true);
}