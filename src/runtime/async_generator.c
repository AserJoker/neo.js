#include "runtime/async_generator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "engine/context.h"
#include "engine/exception.h"
#include "engine/interrupt.h"
#include "engine/scope.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/promise.h"
#include "runtime/vm.h"

NEO_JS_CFUNCTION(neo_js_async_generator_task);
NEO_JS_CFUNCTION(neo_js_async_generator_onfulfilled) {
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_interrupt_t interrupt =
      (neo_js_interrupt_t)neo_js_variable_get_internel(self, ctx, "value")
          ->value;
  neo_list_push(interrupt->vm->stack, value);
  neo_js_scope_set_variable(interrupt->scope, value, NULL);
  neo_js_async_generator_task(ctx, self, 0, NULL);
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_async_generator_onrejected) {
  neo_js_variable_t error = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_interrupt_t interrupt =
      (neo_js_interrupt_t)neo_js_variable_get_internel(self, ctx, "value")
          ->value;
  neo_js_variable_t exception = neo_js_context_create_exception(ctx, error);
  neo_js_scope_set_variable(interrupt->scope, exception, NULL);
  interrupt->vm->result = exception;
  interrupt->address = neo_buffer_get_size(interrupt->program->codes);
  neo_js_async_generator_task(ctx, self, 0, NULL);
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_async_generator_yield_onfulfilled) {
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t res = neo_js_context_create_object(ctx, NULL);
  neo_js_variable_set_field(res, ctx,
                            neo_js_context_create_cstring(ctx, "value"), value);
  neo_js_variable_set_field(res, ctx,
                            neo_js_context_create_cstring(ctx, "done"),
                            neo_js_context_get_false(ctx));
  neo_js_promise_callback_resolve(ctx, promise, 1, &res);
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_async_generator_yield_onrejected) {
  neo_js_variable_t error = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_promise_callback_reject(ctx, promise, 1, &error);
  return neo_js_context_get_undefined(ctx);
}
void neo_js_async_generator_resolve_next(neo_js_context_t ctx,
                                         neo_js_variable_t self,
                                         neo_js_variable_t promise,
                                         neo_js_variable_t next) {
  neo_js_variable_set_internal(self, ctx, "value", next);
  if (next->value->type == NEO_JS_TYPE_INTERRUPT) {
    neo_js_interrupt_t interrupt = (neo_js_interrupt_t)next->value;
    neo_js_variable_t value =
        neo_js_context_create_variable(ctx, interrupt->value);
    if (interrupt->type == NEO_JS_INTERRUPT_YIELD) {
      neo_js_variable_t then = NULL;
      if (value->value->type >= NEO_JS_TYPE_OBJECT &&
          ((then = neo_js_variable_get_field(
                value, ctx, neo_js_context_create_cstring(ctx, "then")))
               ->value->type >= NEO_JS_TYPE_FUNCTION)) {
        neo_js_variable_t onfulfilled = neo_js_context_create_cfunction(
            ctx, neo_js_async_generator_yield_onfulfilled, NULL);
        neo_js_variable_set_bind(onfulfilled, ctx, self);
        neo_js_variable_set_closure(onfulfilled, ctx, "promise", promise);
        neo_js_variable_t onrejected = neo_js_context_create_cfunction(
            ctx, neo_js_async_generator_yield_onrejected, NULL);
        neo_js_variable_set_bind(onrejected, ctx, self);
        neo_js_variable_set_closure(onrejected, ctx, "promise", promise);
        neo_js_variable_t args[] = {onfulfilled, onrejected};
        neo_js_variable_call(then, ctx, value, 2, args);
      } else {
        neo_js_variable_t res = neo_js_context_create_object(ctx, NULL);
        neo_js_variable_set_field(
            res, ctx, neo_js_context_create_cstring(ctx, "value"), value);
        neo_js_variable_set_field(res, ctx,
                                  neo_js_context_create_cstring(ctx, "done"),
                                  neo_js_context_get_false(ctx));
        neo_js_promise_callback_resolve(ctx, promise, 1, &res);
      }
    } else {
      neo_js_variable_t then = NULL;
      if (value->value->type >= NEO_JS_TYPE_OBJECT &&
          ((then = neo_js_variable_get_field(
                value, ctx, neo_js_context_create_cstring(ctx, "then")))
               ->value->type >= NEO_JS_TYPE_FUNCTION)) {
        neo_js_variable_t onfulfilled = neo_js_context_create_cfunction(
            ctx, neo_js_async_generator_onfulfilled, NULL);
        neo_js_variable_set_bind(onfulfilled, ctx, self);
        neo_js_variable_set_closure(onfulfilled, ctx, "promise", promise);
        neo_js_variable_t onrejected = neo_js_context_create_cfunction(
            ctx, neo_js_async_generator_onrejected, NULL);
        neo_js_variable_set_bind(onrejected, ctx, self);
        neo_js_variable_set_closure(onrejected, ctx, "promise", promise);
        neo_js_variable_t args[] = {onfulfilled, onrejected};
        neo_js_variable_call(then, ctx, value, 2, args);
      } else {
        neo_js_variable_t task = neo_js_context_create_cfunction(
            ctx, neo_js_async_generator_task, NULL);
        neo_js_variable_set_closure(task, ctx, "promise", promise);
        neo_js_variable_set_bind(task, ctx, self);
        neo_list_push(interrupt->vm->stack, value);
        neo_js_scope_set_variable(interrupt->scope, value, NULL);
        neo_js_context_create_micro_task(ctx, task);
      }
    }
  } else {
    if (next->value->type == NEO_JS_TYPE_EXCEPTION) {
      neo_js_variable_set_internal(self, ctx, "value", next);
      neo_js_variable_t error = neo_js_context_create_variable(
          ctx, ((neo_js_exception_t)next->value)->error);
      neo_js_promise_callback_reject(ctx, promise, 1, &error);
    } else {

      neo_js_variable_t res = neo_js_context_create_object(ctx, NULL);
      neo_js_variable_set_field(
          res, ctx, neo_js_context_create_cstring(ctx, "value"), next);
      neo_js_variable_set_field(res, ctx,
                                neo_js_context_create_cstring(ctx, "done"),
                                neo_js_context_get_true(ctx));
      neo_js_promise_callback_resolve(ctx, promise, 1, &res);
    }
  }
}

NEO_JS_CFUNCTION(neo_js_async_generator_task) {
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  neo_js_interrupt_t interrupt = (neo_js_interrupt_t)value->value;
  neo_js_scope_t scope = neo_js_context_set_scope(ctx, interrupt->scope);
  neo_js_variable_t next =
      neo_js_vm_run(interrupt->vm, ctx, interrupt->program, interrupt->address);
  neo_js_scope_set_variable(scope, next, NULL);
  if (next->value->type == NEO_JS_TYPE_INTERRUPT) {
    interrupt->vm = NULL;
  } else {
    while (neo_js_context_get_scope(ctx) !=
           neo_js_context_get_root_scope(ctx)) {
      neo_js_context_pop_scope(ctx);
    }
  }
  neo_js_context_set_scope(ctx, scope);
  neo_js_async_generator_resolve_next(ctx, self, promise, next);
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_async_generator_next_task) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t arg = neo_js_context_load(ctx, "arg");
  if (value->value->type == NEO_JS_TYPE_INTERRUPT) {
    neo_js_interrupt_t interrupt = (neo_js_interrupt_t)value->value;
    neo_js_scope_set_variable(interrupt->scope, arg, NULL);
    neo_list_push(interrupt->vm->stack, arg);
    neo_js_async_generator_task(ctx, self, 0, NULL);
  } else if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)value->value)->error);
    neo_js_promise_callback_reject(ctx, promise, 1, &error);
  } else {
    neo_js_variable_t res = neo_js_context_create_object(ctx, NULL);
    neo_js_variable_set_field(
        res, ctx, neo_js_context_create_cstring(ctx, "value"), value);
    neo_js_variable_set_field(res, ctx,
                              neo_js_context_create_cstring(ctx, "done"),
                              neo_js_context_get_true(ctx));
    neo_js_promise_callback_resolve(ctx, promise, 1, &res);
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_async_generator_next) {
  neo_js_variable_t chain = neo_js_variable_get_internel(self, ctx, "chain");
  neo_js_variable_t promise = neo_js_context_create_promise(ctx);
  neo_js_variable_t task = neo_js_context_create_cfunction(
      ctx, neo_js_async_generator_next_task, NULL);
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_set_bind(task, ctx, self);
  neo_js_variable_set_closure(task, ctx, "promise", promise);
  neo_js_variable_set_closure(task, ctx, "arg", arg);
  if (chain) {
    neo_js_variable_t then = neo_js_variable_get_field(
        chain, ctx, neo_js_context_create_cstring(ctx, "finally"));
    neo_js_variable_call(then, ctx, chain, 1, &task);
  } else {
    neo_js_context_create_micro_task(ctx, task);
  }
  neo_js_variable_set_internal(self, ctx, "chain", promise);
  return promise;
}

NEO_JS_CFUNCTION(neo_js_async_generator_return_task) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t arg = neo_js_context_load(ctx, "arg");
  if (value->value->type == NEO_JS_TYPE_INTERRUPT) {
    neo_js_interrupt_t interrupt = (neo_js_interrupt_t)value->value;
    neo_js_scope_set_variable(interrupt->scope, arg, NULL);
    interrupt->vm->result = arg;
    interrupt->address = neo_buffer_get_size(interrupt->program->codes);
    neo_js_async_generator_task(ctx, self, 0, NULL);
  } else if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)value->value)->error);
    neo_js_promise_callback_reject(ctx, promise, 1, &error);
  } else {
    neo_js_variable_t res = neo_js_context_create_object(ctx, NULL);
    neo_js_variable_set_field(
        res, ctx, neo_js_context_create_cstring(ctx, "value"), value);
    neo_js_variable_set_field(res, ctx,
                              neo_js_context_create_cstring(ctx, "done"),
                              neo_js_context_get_true(ctx));
    neo_js_promise_callback_resolve(ctx, promise, 1, &res);
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_async_generator_return) {
  neo_js_variable_t chain = neo_js_variable_get_internel(self, ctx, "chain");
  neo_js_variable_t promise = neo_js_context_create_promise(ctx);
  neo_js_variable_t task = neo_js_context_create_cfunction(
      ctx, neo_js_async_generator_return_task, NULL);
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_set_bind(task, ctx, self);
  neo_js_variable_set_closure(task, ctx, "promise", promise);
  neo_js_variable_set_closure(task, ctx, "arg", arg);
  if (chain) {
    neo_js_variable_t then = neo_js_variable_get_field(
        chain, ctx, neo_js_context_create_cstring(ctx, "finally"));
    neo_js_variable_call(then, ctx, chain, 1, &task);
  } else {
    neo_js_context_create_micro_task(ctx, task);
  }
  neo_js_variable_set_internal(self, ctx, "chain", promise);
  return promise;
}

NEO_JS_CFUNCTION(neo_js_async_generator_throw_task) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t arg = neo_js_context_load(ctx, "arg");
  if (value->value->type == NEO_JS_TYPE_INTERRUPT) {
    arg = neo_js_context_create_exception(ctx, arg);
    neo_js_interrupt_t interrupt = (neo_js_interrupt_t)value->value;
    neo_js_scope_set_variable(interrupt->scope, arg, NULL);
    interrupt->vm->result = arg;
    interrupt->address = neo_buffer_get_size(interrupt->program->codes);
    neo_js_async_generator_task(ctx, self, 0, NULL);
  } else if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)value->value)->error);
    neo_js_promise_callback_reject(ctx, promise, 1, &error);
  } else {
    neo_js_variable_t res = neo_js_context_create_object(ctx, NULL);
    neo_js_variable_set_field(
        res, ctx, neo_js_context_create_cstring(ctx, "value"), value);
    neo_js_variable_set_field(res, ctx,
                              neo_js_context_create_cstring(ctx, "done"),
                              neo_js_context_get_true(ctx));
    neo_js_promise_callback_resolve(ctx, promise, 1, &res);
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_async_generator_throw) {
  neo_js_variable_t chain = neo_js_variable_get_internel(self, ctx, "chain");
  neo_js_variable_t promise = neo_js_context_create_promise(ctx);
  neo_js_variable_t task = neo_js_context_create_cfunction(
      ctx, neo_js_async_generator_throw_task, NULL);
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_set_bind(task, ctx, self);
  neo_js_variable_set_closure(task, ctx, "promise", promise);
  neo_js_variable_set_closure(task, ctx, "arg", arg);
  if (chain) {
    neo_js_variable_t then = neo_js_variable_get_field(
        chain, ctx, neo_js_context_create_cstring(ctx, "finally"));
    neo_js_variable_call(then, ctx, chain, 1, &task);
  } else {
    neo_js_context_create_micro_task(ctx, task);
  }
  neo_js_variable_set_internal(self, ctx, "chain", promise);
  return promise;
}
void neo_initialize_js_async_generator(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->async_generator_prototype =
      neo_js_context_create_object(ctx, constant->iterator_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->async_generator_prototype, "next",
                    neo_js_async_generator_next);
  NEO_JS_DEF_METHOD(ctx, constant->async_generator_prototype, "return",
                    neo_js_async_generator_return);
  NEO_JS_DEF_METHOD(ctx, constant->async_generator_prototype, "throw",
                    neo_js_async_generator_throw);
  neo_js_variable_t string_tag =
      neo_js_context_create_cstring(ctx, "AsyncGenerator");
  neo_js_variable_set_field(constant->async_generator_prototype, ctx,
                            constant->symbol_to_string_tag, string_tag);
}