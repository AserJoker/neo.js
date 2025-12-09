#include "engine/context.h"
#include "compiler/parser.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/bigint.h"
#include "core/clock.h"
#include "core/error.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/bigint.h"
#include "engine/cfunction.h"
#include "engine/exception.h"
#include "engine/function.h"
#include "engine/handle.h"
#include "engine/interrupt.h"
#include "engine/null.h"
#include "engine/number.h"
#include "engine/object.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/signal.h"
#include "engine/stackframe.h"
#include "engine/string.h"
#include "engine/symbol.h"
#include "engine/task.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include "runtime/promise.h"
#include "runtime/vm.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t root_scope;
  neo_js_scope_t current_scope;
  neo_list_t callstack;
  neo_list_t macro_tasks;
  neo_list_t micro_tasks;
  neo_js_variable_t taskroot;
  struct _neo_js_constant_t constant;
  neo_js_context_type_t type;
  neo_js_error_callback onerror;
};

static void neo_js_context_delete_task(neo_js_context_t self,
                                       neo_js_task_t task) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_value_remove_parent(task->callee, self->taskroot->value);
  neo_js_context_create_variable(self, task->callee);
  neo_allocator_free(allocator, task);
}
static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t self) {
  while (neo_list_get_size(self->micro_tasks)) {
    neo_list_node_t it = neo_list_get_first(self->micro_tasks);
    neo_js_task_t task = neo_list_node_get(it);
    neo_list_erase(self->micro_tasks, it);
    neo_js_context_delete_task(self, task);
  }
  neo_allocator_free(allocator, self->micro_tasks);
  while (neo_list_get_size(self->macro_tasks)) {
    neo_list_node_t it = neo_list_get_first(self->macro_tasks);
    neo_js_task_t task = neo_list_node_get(it);
    neo_list_erase(self->macro_tasks, it);
    neo_js_context_delete_task(self, task);
  }
  neo_allocator_free(allocator, self->macro_tasks);
  self->root_scope = NULL;
  while (self->current_scope) {
    neo_js_context_pop_scope(self);
  }
  neo_allocator_free(allocator, self->callstack);
}

static void neo_js_context_onerror(neo_js_context_t self,
                                   neo_js_variable_t error) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  if (error->value->type == NEO_JS_TYPE_SYMBOL) {
    neo_js_symbol_t symbol = (neo_js_symbol_t)error->value;
    char *description = neo_string16_to_string(allocator, symbol->description);
    fprintf(stderr, "Uncaught Symbol(%s)\n", description);
    neo_allocator_free(allocator, description);
    return;
  } else if (error->value->type != NEO_JS_TYPE_STRING) {
    error = neo_js_variable_to_string(error, self);
    if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
      neo_js_exception_t exception = (neo_js_exception_t)error->value;
      error = neo_js_context_create_variable(self, exception->error);
      return neo_js_context_onerror(self, error);
    }
  }
  const uint16_t *err = ((neo_js_string_t)error->value)->value;
  char *msg = neo_string16_to_string(allocator, err);
  fprintf(stderr, "Uncaught %s\n", msg);
  neo_allocator_free(allocator, msg);
}

neo_js_context_t neo_create_js_context(neo_js_runtime_t runtime) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root_scope = neo_create_js_scope(allocator, NULL);
  ctx->current_scope = ctx->root_scope;
  ctx->type = NEO_JS_CONTEXT_MODULE;
  neo_list_initialize_t initialize = {true};
  ctx->callstack = neo_create_list(allocator, &initialize);
  ctx->macro_tasks = neo_create_list(allocator, NULL);
  ctx->micro_tasks = neo_create_list(allocator, NULL);
  const char *funcname = "_.start";
  uint16_t fname[16];
  uint16_t *dst = fname;
  while (*funcname) {
    *dst++ = *funcname++;
  }
  *dst = 0;
  neo_js_stackframe_t frame =
      neo_create_js_stackframe(allocator, NULL, fname, 0, 0);
  neo_list_push(ctx->callstack, frame);
  memset(&ctx->constant, 0, sizeof(struct _neo_js_constant_t));
  ctx->onerror = neo_js_context_onerror;
  neo_js_context_push_scope(ctx);
  neo_initialize_js_constant(ctx);
  neo_js_context_pop_scope(ctx);
  neo_js_context_push_scope(ctx);
  ctx->taskroot = neo_js_context_create_variable(
      ctx, &neo_create_js_null(allocator)->super);
  return ctx;
}
void neo_js_context_set_error_callback(neo_js_context_t self,
                                       neo_js_error_callback callback) {
  self->onerror = callback;
}
neo_js_error_callback neo_js_context_get_error_callback(neo_js_context_t self) {
  return self->onerror;
}

neo_js_constant_t neo_js_context_get_constant(neo_js_context_t self) {
  return &self->constant;
}

neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self) {
  return self->runtime;
}
neo_js_context_type_t neo_js_context_get_type(neo_js_context_t self) {
  return self->type;
}
neo_js_context_type_t neo_js_context_set_type(neo_js_context_t self,
                                              neo_js_context_type_t type) {
  neo_js_context_type_t current = self->type;
  self->type = type;
  return current;
}

void neo_js_context_push_scope(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  self->current_scope = neo_create_js_scope(allocator, self->current_scope);
}

static void neo_js_context_ongc(neo_allocator_t allocator,
                                neo_js_handle_t handle, neo_js_context_t ctx) {
  if (handle->type == NEO_JS_HANDLE_VALUE) {
    neo_js_value_t value = (neo_js_value_t)handle;
    neo_js_promise_t promise = neo_hash_map_get(value->opaque, "promise");
    if (promise && promise->value &&
        promise->value->type == NEO_JS_TYPE_EXCEPTION &&
        neo_list_get_size(promise->onrejected) == 0) {
      neo_js_exception_t exception = (neo_js_exception_t)promise->value;
      neo_js_variable_t error =
          neo_js_context_create_variable(ctx, exception->error);
      ctx->onerror(ctx, error);
    }
  }
}

static void neo_js_context_scope_gc(neo_js_context_t self,
                                    neo_js_scope_t scope) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_list_t variables = neo_js_scope_get_variables(scope);
  neo_list_node_t it = neo_list_get_first(variables);
  while (it != neo_list_get_tail(variables)) {
    neo_js_variable_t variable = neo_list_node_get(it);
    it = neo_list_node_next(it);
    neo_js_handle_remove_parent(&variable->handle, (neo_js_handle_t)scope);
  }
  neo_list_initialize_t initialize = {true};
  neo_list_t destroyed = neo_create_list(allocator, &initialize);
  neo_js_handle_gc(allocator, variables, destroyed,
                   (neo_js_handle_on_gc_fn_t)neo_js_context_ongc, self);
  neo_allocator_free(allocator, destroyed);
  neo_allocator_free(allocator, scope);
}

static void neo_js_context_dispose_scope(neo_js_context_t self,
                                         neo_js_scope_t scope) {
  neo_list_t children = neo_js_scope_get_children(scope);
  while (neo_list_get_size(children)) {
    neo_js_scope_t child = neo_list_node_get(neo_list_get_first(children));
    neo_js_context_dispose_scope(self, child);
  }
  self->current_scope = neo_js_scope_get_parent(scope);
  neo_js_context_scope_gc(self, scope);
}

void neo_js_context_pop_scope(neo_js_context_t self) {
  neo_js_context_dispose_scope(self, self->current_scope);
}

neo_js_scope_t neo_js_context_get_scope(neo_js_context_t self) {
  return self->current_scope;
}
neo_js_scope_t neo_js_context_get_root_scope(neo_js_context_t self) {
  return self->root_scope;
}
neo_js_scope_t neo_js_context_set_scope(neo_js_context_t self,
                                        neo_js_scope_t scope) {
  neo_js_scope_t current = self->current_scope;
  self->current_scope = scope;
  return current;
}

void neo_js_context_push_callstack(neo_js_context_t self, const char *filename,
                                   const uint16_t *funcname, uint32_t line,
                                   uint32_t column) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_list_node_t last = neo_list_get_last(self->callstack);
  neo_js_stackframe_t frame = neo_list_node_get(last);
  frame->column = column;
  frame->line = line;
  if (filename) {
    frame->filename = filename;
  }
  frame = neo_create_js_stackframe(allocator, NULL, funcname, 0, 0);
  neo_list_push(self->callstack, frame);
}
void neo_js_context_pop_callstack(neo_js_context_t self) {
  neo_list_pop(self->callstack);
}
neo_list_t neo_js_context_get_callstack(neo_js_context_t self) {
  return self->callstack;
}
neo_list_t neo_js_context_set_callstack(neo_js_context_t self,
                                        neo_list_t callstack) {
  neo_list_t current = self->callstack;
  self->callstack = callstack;
  return current;
}
neo_list_t neo_js_context_trace(neo_js_context_t self, const char *filename,
                                uint32_t line, uint32_t column) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_list_initialize_t initialize = {true};
  neo_list_t callstack = neo_create_list(allocator, &initialize);
  neo_list_node_t it = neo_list_get_first(self->callstack);
  while (it != neo_list_get_tail(self->callstack)) {
    neo_js_stackframe_t frame = neo_list_node_get(it);
    frame =
        neo_create_js_stackframe(allocator, frame->filename, frame->funcname,
                                 frame->line, frame->column);
    neo_list_push(callstack, frame);
    it = neo_list_node_next(it);
  }
  it = neo_list_get_last(callstack);
  neo_js_stackframe_t frame = neo_list_node_get(it);
  frame->column = column;
  frame->line = line;
  if (filename) {
    frame->filename = filename;
  }
  return callstack;
}

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t self,
                                                 neo_js_value_t value) {
  return neo_js_scope_create_variable(self->current_scope, value, NULL);
}

neo_js_variable_t neo_js_context_get_uninitialized(neo_js_context_t self) {
  return self->constant.uninitialized;
}
neo_js_variable_t neo_js_context_get_undefined(neo_js_context_t self) {
  return self->constant.undefined;
}
neo_js_variable_t neo_js_context_get_null(neo_js_context_t self) {
  return self->constant.null;
}
neo_js_variable_t neo_js_context_get_nan(neo_js_context_t self) {
  return self->constant.nan;
}
neo_js_variable_t neo_js_context_get_infinity(neo_js_context_t self) {
  return self->constant.infinity;
}
neo_js_variable_t neo_js_context_get_true(neo_js_context_t self) {
  return self->constant.boolean_true;
}
neo_js_variable_t neo_js_context_get_false(neo_js_context_t self) {
  return self->constant.boolean_false;
}
neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double val) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_number_t number = neo_create_js_number(allocator, val);
  neo_js_value_t value = neo_js_number_to_value(number);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               const uint16_t *val) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_string_t string = neo_create_js_string(allocator, val);
  neo_js_value_t value = neo_js_string_to_value(string);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_cstring(neo_js_context_t self,
                                                const char *val) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  uint16_t *str = neo_string_to_string16(allocator, val);
  neo_js_variable_t string = neo_js_context_create_string(self, str);
  neo_allocator_free(allocator, str);
  return string;
}
neo_js_variable_t neo_js_context_create_cbigint(neo_js_context_t self,
                                                const char *string) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_bigint_t val = neo_string_to_bigint(allocator, string);
  if (!val) {
    char msg[strlen(string) + 64];
    sprintf(msg, "Cannot convert %s to a Bigint", string);
    neo_js_variable_t message = neo_js_context_create_cstring(self, msg);
    neo_js_variable_t error = neo_js_variable_construct(
        self->constant.syntax_error_class, self, 1, &message);
    return neo_js_context_create_exception(self, error);
  }
  neo_js_bigint_t bigint = neo_create_js_bigint(allocator, val);
  neo_js_value_t value = neo_js_bigint_to_value(bigint);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_bigint(neo_js_context_t self,
                                               neo_bigint_t val) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_bigint_t bigint = neo_create_js_bigint(allocator, val);
  neo_js_value_t value = neo_js_bigint_to_value(bigint);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t self,
                                               const uint16_t *description) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_symbol_t symbol = neo_create_js_symbol(allocator, description);
  neo_js_value_t value = neo_js_symbol_to_value(symbol);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_exception(neo_js_context_t self,
                                                  neo_js_variable_t error) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_exception_t exception =
      neo_create_js_exception(allocator, error->value);
  neo_js_value_t value = neo_js_exception_to_value(exception);
  neo_js_variable_t result = neo_js_context_create_variable(self, value);
  return result;
}

neo_js_variable_t neo_js_context_create_object(neo_js_context_t self,
                                               neo_js_variable_t prototype) {
  if (!prototype) {
    prototype = self->constant.object_prototype;
  }
  if (!prototype) {
    prototype = neo_js_context_get_null(self);
  }
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_object_t object = neo_create_js_object(allocator, prototype->value);
  neo_js_value_t value = neo_js_object_to_value(object);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_array(neo_js_context_t ctx) {
  return neo_js_variable_construct(ctx->constant.array_class, ctx, 0, NULL);
}
neo_js_variable_t neo_js_context_create_cfunction(neo_js_context_t self,
                                                  neo_js_cfunc_t callee,
                                                  const char *name) {
  neo_js_variable_t prototype = self->constant.function_prototype;
  if (!prototype) {
    prototype = neo_js_context_get_null(self);
  }
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_cfunction_t cfunction =
      neo_create_js_cfunction(allocator, callee, prototype->value);
  neo_js_value_t value = neo_js_cfunction_to_value(cfunction);
  neo_js_variable_t result = neo_js_context_create_variable(self, value);
  prototype = neo_js_context_create_object(self, NULL);
  neo_js_variable_t key = self->constant.key_prototype;
  neo_js_variable_def_field(result, self, key, prototype, true, false, true);
  key = self->constant.key_name;
  if (!name) {
    name = "";
  }
  neo_js_variable_t funcname = neo_js_context_create_cstring(self, name);
  neo_js_variable_def_field(result, self, key, funcname, false, false, false);
  return result;
}

neo_js_variable_t neo_js_context_create_function(neo_js_context_t self,
                                                 neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_function_t function = neo_create_js_function(
      allocator, program, self->constant.function_prototype->value);
  neo_js_value_t value = neo_js_function_to_value(function);
  neo_js_variable_t result = neo_js_context_create_variable(self, value);
  neo_js_variable_t prototype = neo_js_context_create_object(self, NULL);
  neo_js_variable_t key = self->constant.key_prototype;
  neo_js_variable_def_field(result, self, key, prototype, true, false, true);
  neo_js_variable_t funcname = neo_js_context_create_cstring(self, "");
  key = self->constant.key_name;
  neo_js_variable_def_field(result, self, key, funcname, false, false, false);
  return result;
}
neo_js_variable_t neo_js_context_create_async_function(neo_js_context_t self,
                                                       neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_function_t function = neo_create_js_function(
      allocator, program, self->constant.async_function_prototype->value);
  function->super.async = true;
  neo_js_value_t value = neo_js_function_to_value(function);
  neo_js_variable_t result = neo_js_context_create_variable(self, value);
  neo_js_variable_t prototype = neo_js_context_create_object(self, NULL);
  neo_js_variable_t key = self->constant.key_prototype;
  neo_js_variable_def_field(result, self, key, prototype, true, false, true);
  neo_js_variable_t funcname = neo_js_context_create_cstring(self, "");
  key = self->constant.key_name;
  neo_js_variable_def_field(result, self, key, funcname, false, false, false);
  return result;
}
neo_js_variable_t
neo_js_context_create_generator_function(neo_js_context_t self,
                                         neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_function_t function = neo_create_js_function(
      allocator, program, self->constant.generator_function_prototype->value);
  function->super.generator = true;
  neo_js_value_t value = neo_js_function_to_value(function);
  neo_js_variable_t result = neo_js_context_create_variable(self, value);
  neo_js_variable_t prototype = neo_js_context_create_object(self, NULL);
  neo_js_variable_t key = self->constant.key_prototype;
  neo_js_variable_def_field(result, self, key, prototype, true, false, true);
  neo_js_variable_t funcname = neo_js_context_create_cstring(self, "");
  key = self->constant.key_name;
  neo_js_variable_def_field(result, self, key, funcname, false, false, false);
  return result;
}
neo_js_variable_t
neo_js_context_create_async_generator_function(neo_js_context_t self,
                                               neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_function_t function = neo_create_js_function(
      allocator, program,
      self->constant.async_generator_function_prototype->value);
  function->super.generator = true;
  function->super.async = true;
  neo_js_value_t value = neo_js_function_to_value(function);
  neo_js_variable_t result = neo_js_context_create_variable(self, value);
  neo_js_variable_t prototype = neo_js_context_create_object(self, NULL);
  neo_js_variable_t key = self->constant.key_prototype;
  neo_js_variable_def_field(result, self, key, prototype, true, false, true);
  neo_js_variable_t funcname = neo_js_context_create_cstring(self, "");
  key = self->constant.key_name;
  neo_js_variable_def_field(result, self, key, funcname, false, false, false);
  return result;
}
neo_js_variable_t neo_js_context_create_signal(neo_js_context_t self,
                                               uint32_t type, const void *msg) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_signal_t signal = neo_create_js_signal(allocator, type, msg);
  return neo_js_context_create_variable(self, &signal->super);
}
neo_js_variable_t
neo_js_context_create_interrupt(neo_js_context_t self, neo_js_variable_t value,
                                size_t address, neo_program_t program,
                                neo_js_vm_t vm, neo_js_interrupt_type_t type) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_interrupt_t interrupt = neo_create_js_interrupt(
      allocator, value, address, program, vm, self->current_scope, type);
  return neo_js_context_create_variable(self, &interrupt->super);
}
neo_js_variable_t neo_js_context_create_promise(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_variable_t object =
      neo_js_context_create_object(self, self->constant.promise_prototype);
  neo_js_promise_t promise = neo_create_js_promise(allocator);
  neo_js_variable_set_opaque(object, self, "promise", promise);
  return object;
}

neo_js_variable_t neo_js_context_load(neo_js_context_t self, const char *name) {
  neo_js_variable_t variable = NULL;
  neo_js_scope_t scope = self->current_scope;
  while (scope) {
    variable = neo_js_scope_get_variable(scope, name);
    if (variable) {
      break;
    }
    scope = neo_js_scope_get_parent(scope);
  }
  if (!variable) {
    neo_js_object_t object = (neo_js_object_t)(self->constant.global->value);
    neo_hash_map_node_t it = neo_hash_map_get_first(object->properties);
    while (it != neo_hash_map_get_tail(object->properties)) {
      neo_js_value_t key = neo_hash_map_node_get_key(it);
      if (key->type == NEO_JS_TYPE_STRING) {
        neo_js_string_t string = (neo_js_string_t)key;
        if (neo_string16_mix_compare(string->value, name) == 0) {
          neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
          if (prop->value) {
            variable = neo_js_context_create_variable(self, prop->value);
          } else if (prop->get) {
            neo_js_variable_t get =
                neo_js_context_create_variable(self, prop->get);
            variable =
                neo_js_variable_call(get, self, self->constant.global, 0, NULL);
          } else {
            variable = self->constant.undefined;
          }
          break;
        }
      }
      it = neo_hash_map_node_next(it);
    }
  }
  if (!variable) {
    const char *stuffix = " is not defined";
    uint16_t msg[strlen(name) + 16];
    uint16_t *dst = msg;
    const char *src = name;
    while (*src) {
      *dst++ = *src++;
    }
    while (*stuffix) {
      *dst++ = *stuffix++;
    }
    *dst = 0;
    neo_js_variable_t message = neo_js_context_create_string(self, msg);
    neo_js_variable_t error = neo_js_variable_construct(
        self->constant.reference_error_class, self, 1, &message);
    return neo_js_context_create_exception(self, error);
  }
  return variable;
}

neo_js_variable_t neo_js_context_store(neo_js_context_t self, const char *name,
                                       neo_js_variable_t variable) {
  neo_js_variable_t current = neo_js_context_load(self, name);
  if (current->value->type == NEO_JS_TYPE_EXCEPTION) {
    return current;
  }
  if (current->is_const && current->value->type != NEO_JS_TYPE_UNINITIALIZED) {
    neo_js_variable_t message =
        neo_js_context_create_cstring(self, "Assignment to constant variable");
    neo_js_variable_t error = neo_js_variable_construct(
        self->constant.reference_error_class, self, 1, &message);
    return neo_js_context_create_exception(self, error);
  }
  neo_js_context_create_variable(self, current->value);
  neo_js_handle_remove_parent(&current->value->handle, &current->handle);
  current->value = variable->value;
  neo_js_handle_add_parent(&current->value->handle, &current->handle);
  return current;
}
neo_js_variable_t neo_js_context_def(neo_js_context_t self, const char *name,
                                     neo_js_variable_t variable) {
  neo_js_scope_create_variable(self->current_scope, variable->value, name);
  return variable;
}

neo_js_variable_t neo_js_context_get_global(neo_js_context_t self) {
  return self->constant.global;
}

void neo_js_context_recycle(neo_js_context_t self, neo_js_value_t value) {
  neo_js_scope_t scope = self->current_scope;
}

neo_js_variable_t neo_js_context_format(neo_js_context_t self, const char *fmt,
                                        ...) {
  size_t len = 0;
  va_list args;
  va_start(args, fmt);
  const char *psrc = fmt;
  while (*psrc) {
    neo_utf8_char chr = neo_utf8_read_char(psrc);
    if (*psrc == '%') {
      len++;
      psrc++;
      if (*psrc == 0) {
        break;
      } else if (*psrc == 'v') {
        psrc++;
        neo_js_variable_t arg = va_arg(args, neo_js_variable_t);
        if (arg->value->type == NEO_JS_TYPE_SYMBOL) {
          neo_js_symbol_t symbol = (neo_js_symbol_t)arg->value;
          len += neo_string16_length(symbol->description) + 8;
        } else {
          arg = neo_js_variable_to_string(arg, self);
          neo_js_string_t string = (neo_js_string_t)arg->value;
          len += neo_string16_length(string->value);
        }
        continue;
      }
    }
    psrc = chr.end;
    uint32_t utf32 = neo_utf8_char_to_utf32(chr);
    len += neo_utf32_to_utf16(utf32, NULL);
  }
  va_end(args);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  uint16_t *string =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);

  va_start(args, fmt);
  psrc = fmt;
  uint16_t *dst = string;
  while (*psrc) {
    neo_utf8_char chr = neo_utf8_read_char(psrc);
    if (*psrc == '%') {
      psrc++;
      if (*psrc == 0) {
        break;
      } else if (*psrc == 'v') {
        psrc++;
        neo_js_variable_t arg = va_arg(args, neo_js_variable_t);
        if (arg->value->type == NEO_JS_TYPE_SYMBOL) {
          neo_js_symbol_t symbol = (neo_js_symbol_t)arg->value;
          const char *prefix = "Symbol(";
          const char *stuffix = ")";
          while (*prefix) {
            *dst++ = *prefix++;
          }
          const uint16_t *src = symbol->description;
          while (*src) {
            *dst++ = *src++;
          }
          while (*stuffix) {
            *dst++ = *stuffix++;
          }
        } else {
          arg = neo_js_variable_to_string(arg, self);
          neo_js_string_t string = (neo_js_string_t)arg->value;
          const uint16_t *src = string->value;
          while (*src) {
            *dst++ = *src++;
          }
        }
        continue;
      }
    }
    psrc = chr.end;
    uint32_t utf32 = neo_utf8_char_to_utf32(chr);
    dst += neo_utf32_to_utf16(utf32, dst);
  }
  va_end(args);
  *dst = 0;
  neo_js_variable_t result = neo_js_context_create_string(self, string);
  neo_allocator_free(allocator, string);
  return result;
}
neo_js_variable_t neo_js_context_get_argument(neo_js_context_t self,
                                              size_t argc,
                                              neo_js_variable_t *argv,
                                              size_t idx) {
  if (idx >= argc) {
    return neo_js_context_get_undefined(self);
  }
  return argv[idx];
}

int64_t neo_js_context_create_macro_task(neo_js_context_t self,
                                         neo_js_variable_t callee,
                                         int64_t timeout, bool keep) {
  static size_t idx = 0;
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_task_t task =
      neo_create_js_task(allocator, ++idx, callee->value, timeout, keep);
  neo_js_value_add_parent(callee->value, self->taskroot->value);
  neo_list_push(self->macro_tasks, task);
  return idx;
}

void neo_js_context_remove_macro_task(neo_js_context_t self, int64_t idx) {
  for (neo_list_node_t it = neo_list_get_last(self->macro_tasks);
       it != neo_list_get_head(self->macro_tasks);
       it = neo_list_node_last(it)) {
    neo_js_task_t task = neo_list_node_get(it);
    if (task->idx == idx) {
      neo_list_erase(self->macro_tasks, it);
      neo_js_context_delete_task(self, task);
      return;
    }
  }
}

int64_t neo_js_context_create_micro_task(neo_js_context_t self,
                                         neo_js_variable_t callee) {
  static size_t idx = 0;
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_task_t task =
      neo_create_js_task(allocator, ++idx, callee->value, 0, false);
  neo_js_value_add_parent(callee->value, self->taskroot->value);
  neo_list_push(self->micro_tasks, task);
  return idx;
}

void neo_js_context_remove_micro_task(neo_js_context_t self, int64_t idx) {
  for (neo_list_node_t it = neo_list_get_last(self->micro_tasks);
       it != neo_list_get_head(self->micro_tasks);
       it = neo_list_node_last(it)) {
    neo_js_task_t task = neo_list_node_get(it);
    if (task->idx == idx) {
      neo_list_erase(self->micro_tasks, it);
      neo_js_context_delete_task(self, task);
      return;
    }
  }
}

static bool neo_js_context_exec_next_task(neo_js_context_t self,
                                          neo_list_t tasks) {
  if (neo_list_get_size(tasks) == 0) {
    return false;
  }
  neo_list_node_t it = neo_list_get_first(tasks);
  neo_js_task_t task = neo_list_node_get(it);
  neo_list_erase(tasks, it);
  int64_t now = neo_clock_get_timestamp();
  if (now - task->start >= task->time) {
    neo_js_context_push_scope(self);
    neo_js_variable_t callee =
        neo_js_context_create_variable(self, task->callee);
    neo_js_variable_t res =
        neo_js_variable_call(callee, self, self->constant.undefined, 0, NULL);
    if (!task->keep) {
      neo_js_context_delete_task(self, task);
    } else {
      task->start = neo_clock_get_timestamp();
      neo_list_push(tasks, task);
    }
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      neo_js_exception_t exception = (neo_js_exception_t)res->value;
      neo_js_variable_t error =
          neo_js_context_create_variable(self, exception->error);
      self->onerror(self, error);
    }
    neo_js_context_pop_scope(self);
    return true;
  } else {
    neo_list_push(tasks, task);
    return false;
  }
}

bool neo_js_context_next_task(neo_js_context_t self) {
  if (neo_js_context_exec_next_task(self, self->micro_tasks)) {
    return true;
  }
  return neo_js_context_exec_next_task(self, self->macro_tasks);
}
bool neo_js_context_has_task(neo_js_context_t self) {
  return neo_list_get_size(self->micro_tasks) != 0 ||
         neo_list_get_size(self->macro_tasks) != 0;
}

neo_js_variable_t neo_js_context_eval(neo_js_context_t self, const char *source,
                                      const char *filename) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_ast_node_t node = neo_ast_parse_code(allocator, filename, source);
  if (neo_has_error()) {
    neo_error_t err = neo_poll_error(NULL, NULL, 0);
    const char *msg = neo_error_get_message(err);
    neo_js_variable_t message = neo_js_context_create_cstring(self, msg);
    neo_allocator_free(allocator, err);
    neo_js_variable_t error = neo_js_variable_construct(
        self->constant.syntax_error_class, self, 1, &message);
    return neo_js_context_create_exception(self, error);
  }
  neo_program_t program = neo_ast_write_node(allocator, filename, node);
  neo_allocator_free(allocator, node);
  if (neo_has_error()) {
    neo_error_t err = neo_poll_error(NULL, NULL, 0);
    const char *msg = neo_error_get_message(err);
    neo_js_variable_t message = neo_js_context_create_cstring(self, msg);
    neo_allocator_free(allocator, err);
    neo_js_variable_t error = neo_js_variable_construct(
        self->constant.syntax_error_class, self, 1, &message);
    return neo_js_context_create_exception(self, error);
  }
  neo_js_runtime_set_program(self->runtime, filename, program);
  neo_js_scope_t scope = neo_create_js_scope(allocator, self->root_scope);
  neo_js_scope_t origin_scope = neo_js_context_set_scope(self, scope);
  neo_js_vm_t vm = neo_create_js_vm(self, NULL);
  neo_js_variable_t result = neo_js_vm_run(vm, self, program, 0);
  if (result->value->type != NEO_JS_TYPE_INTERRUPT) {
    neo_allocator_free(allocator, vm);
    neo_js_scope_set_variable(origin_scope, result, NULL);
    while (self->current_scope != self->root_scope) {
      neo_js_context_pop_scope(self);
    }
  } else {
    neo_js_interrupt_t interrupt = (neo_js_interrupt_t)result->value;
    neo_js_variable_t value =
        neo_js_context_create_variable(self, interrupt->value);
    neo_js_variable_t then = NULL;
    neo_js_variable_t promise = neo_js_context_create_promise(self);
    if (value->value->type == NEO_JS_TYPE_OBJECT &&
        ((then = neo_js_variable_get_field(
              value, self, neo_js_context_create_cstring(self, "then")))
             ->value->type >= NEO_JS_TYPE_FUNCTION)) {
      neo_js_variable_t onfulfilled =
          neo_js_context_create_cfunction(self, neo_js_async_onfulfilled, NULL);
      neo_js_variable_set_closure(onfulfilled, self, "promise", promise);
      neo_js_variable_set_closure(onfulfilled, self, "interrupt", result);
      neo_js_variable_t onrejected =
          neo_js_context_create_cfunction(self, neo_js_async_onrejected, NULL);
      neo_js_variable_set_closure(onrejected, self, "promise", promise);
      neo_js_variable_set_closure(onrejected, self, "interrupt", result);
      neo_js_variable_t args[] = {onfulfilled, onrejected};
      neo_js_variable_call(then, self, value, 2, args);
    } else {
      neo_js_variable_t task =
          neo_js_context_create_cfunction(self, neo_js_async_task, NULL);
      neo_js_variable_set_closure(task, self, "promise", promise);
      neo_js_variable_set_closure(task, self, "interrupt", result);
      neo_js_context_create_micro_task(self, task);
    }
    neo_js_scope_set_variable(origin_scope, promise, NULL);
    neo_js_context_set_scope(self, origin_scope);
    return promise;
  }
  neo_js_context_set_scope(self, origin_scope);
  return result;
}