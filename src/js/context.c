#include "js/context.h"
#include "core/allocator.h"
#include "core/list.h"
#include "js/boolean.h"
#include "js/error.h"
#include "js/function.h"
#include "js/handle.h"
#include "js/infinity.h"
#include "js/nan.h"
#include "js/number.h"
#include "js/object.h"
#include "js/runtime.h"
#include "js/scope.h"
#include "js/stackframe.h"
#include "js/string.h"
#include "js/symbol.h"
#include "js/type.h"
#include "js/undefined.h"
#include "js/value.h"
#include "js/variable.h"
#include <wchar.h>
struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t scope;
  neo_js_scope_t root;
  neo_js_variable_t global;
  neo_list_t stacktrace;

  struct {
    neo_js_variable_t object_constructor;
    neo_js_variable_t function_constructor;
    neo_js_variable_t number_constructor;
    neo_js_variable_t boolean_constructor;
    neo_js_variable_t string_constructor;
    neo_js_variable_t symbol_constructor;
    neo_js_variable_t array_constructor;
  } std;
};

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t ctx) {
  neo_allocator_free(allocator, ctx->root);
  ctx->scope = NULL;
  ctx->root = NULL;
  ctx->global = NULL;
  ctx->runtime = NULL;
  neo_allocator_free(allocator, ctx->stacktrace);
}

neo_js_context_t neo_create_js_context(neo_allocator_t allocator,
                                       neo_js_runtime_t runtime) {
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root = neo_create_js_scope(allocator, NULL);
  ctx->scope = ctx->root;
  ctx->global = NULL;
  neo_list_initialize_t initialize = {true};
  ctx->stacktrace = neo_create_list(allocator, &initialize);
  return ctx;
}

neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t ctx) {
  return ctx->runtime;
}

neo_js_scope_t neo_js_context_get_scope(neo_js_context_t ctx) {
  return ctx->scope;
}

neo_js_scope_t neo_js_context_get_root(neo_js_context_t ctx) {
  return ctx->root;
}

neo_js_variable_t neo_js_context_get_global(neo_js_context_t ctx) {
  return ctx->global;
}

neo_js_scope_t neo_js_context_push_scope(neo_js_context_t ctx) {
  ctx->scope = neo_create_js_scope(neo_js_runtime_get_allocator(ctx->runtime),
                                   ctx->scope);
  return ctx->scope;
}

neo_js_scope_t neo_js_context_pop_scope(neo_js_context_t ctx) {
  neo_js_scope_t current = ctx->scope;
  ctx->scope = neo_js_scope_get_parent(ctx->scope);
  neo_allocator_free(neo_js_runtime_get_allocator(ctx->runtime), current);
  return ctx->scope;
}

neo_list_t neo_js_context_get_stacktrace(neo_js_context_t ctx, uint32_t line,
                                         uint32_t column) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_list_node_t it = neo_list_get_last(ctx->stacktrace);
  neo_js_stackframe_t frame = neo_list_node_get(it);

  frame->column = column;
  frame->line = line;
  return ctx->stacktrace;
}

void neo_js_context_push_stackframe(neo_js_context_t ctx,
                                    const wchar_t *filename,
                                    const wchar_t *function, uint32_t column,
                                    uint32_t line) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_list_node_t it = neo_list_get_last(ctx->stacktrace);
  neo_js_stackframe_t frame = neo_list_node_get(it);

  frame->column = column;
  frame->line = line;
  frame = neo_create_js_stackframe(allocator);
  size_t len = wcslen(function);
  frame->function =
      neo_allocator_alloc(allocator, sizeof(wchar_t) * (len + 1), NULL);
  wcscpy(frame->function, function);
  frame->function[len] = 0;
  frame->filename = filename;
  neo_list_push(ctx->stacktrace, frame);
}

void neo_js_context_pop_stackframe(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_list_pop(ctx->stacktrace);
  neo_list_node_t it = neo_list_get_last(ctx->stacktrace);
  neo_js_stackframe_t frame = neo_list_node_get(it);
  frame->line = 0;
  frame->column = 0;
}

neo_js_variable_t neo_js_context_clone_variable(neo_js_context_t ctx,
                                                neo_js_variable_t variable) {
  neo_js_handle_t handle = neo_js_variable_get_handle(variable);
  return neo_js_context_create_variable(ctx, handle);
}

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t ctx,
                                                 neo_js_handle_t handle) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_variable_t variable = neo_create_js_variable(allocator, handle);
  neo_js_handle_t current = neo_js_scope_get_root_handle(ctx->scope);
  neo_js_handle_add_parent(handle, current);
  neo_js_scope_set_variable(allocator, ctx->scope, variable, NULL);
  return variable;
}

neo_js_variable_t neo_js_context_create_error(neo_js_context_t ctx,
                                              const wchar_t *type,
                                              const wchar_t *message) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_error_t error =
      neo_create_js_error(allocator, type, message, ctx->stacktrace);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &error->value));
}

neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_undefined_t undefined = neo_create_js_undefined(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &undefined->value));
}

neo_js_variable_t neo_js_context_create_nan(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_nan_t nan = neo_create_js_nan(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &nan->value));
}

neo_js_variable_t neo_js_context_create_infinity(neo_js_context_t ctx,
                                                 bool negative) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_infinity_t infinity = neo_create_js_infinity(allocator, negative);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &infinity->value));
}

neo_js_variable_t neo_js_context_create_number(neo_js_context_t ctx,
                                               double value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_number_t number = neo_create_js_number(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &number->value));
}

neo_js_variable_t neo_js_context_create_string(neo_js_context_t ctx,
                                               const wchar_t *value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_string_t string = neo_create_js_string(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value));
}

neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t ctx,
                                                bool value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_boolean_t boolean = neo_create_js_boolean(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &boolean->value));
}

neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t ctx,
                                               wchar_t *description) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_symbol_t symbol = neo_create_js_symbol(allocator, description);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &symbol->value));
}
neo_js_variable_t neo_js_context_create_object(neo_js_context_t ctx,
                                               neo_js_variable_t prototype) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_object_t object = neo_create_js_object(allocator);
  neo_js_handle_t hobject = neo_create_js_handle(allocator, &object->value);
  if (prototype) {
    neo_js_handle_t hproto = neo_js_variable_get_handle(prototype);
    neo_js_handle_add_parent(hproto, hobject);
  } else {
    if (ctx->std.object_constructor) {
      neo_js_handle_t hproto =
          neo_js_variable_get_handle(neo_js_context_get_field(
              ctx, neo_js_context_create_string(ctx, L"prototype"),
              ctx->std.object_constructor));
      neo_js_handle_add_parent(hproto, hobject);
      object->prototype = hproto;
    }
  }
  if (ctx->std.object_constructor) {
    neo_js_handle_t hconstructor =
        neo_js_variable_get_handle(ctx->std.object_constructor);
    neo_js_handle_add_parent(hconstructor, hobject);
    object->constructor = hconstructor;
  }
  return neo_js_context_create_variable(ctx, hobject);
}

neo_js_variable_t
neo_js_context_create_cfunction(neo_js_context_t ctx, const wchar_t *name,
                                neo_js_cfunction_fn_t function) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, function);
  neo_js_handle_t hfunction =
      neo_create_js_handle(allocator, &func->object.value);
  if (ctx->std.function_constructor) {
    neo_js_handle_t hproto = hproto =
        neo_js_variable_get_handle(neo_js_context_get_field(
            ctx, neo_js_context_create_string(ctx, L"prototype"),
            ctx->std.function_constructor));
    func->object.prototype = hproto;
    func->object.constructor =
        neo_js_variable_get_handle(ctx->std.function_constructor);
    neo_js_handle_add_parent(func->object.prototype, hfunction);
    neo_js_handle_add_parent(func->object.constructor, hfunction);
  };
  if (name) {
    neo_js_handle_t hname =
        neo_js_variable_get_handle(neo_js_context_create_string(ctx, name));
    func->name = hname;
    neo_js_handle_add_parent(hname, hfunction);
  }
  return neo_js_context_create_variable(ctx, hfunction);
}

const wchar_t *neo_js_context_typeof(neo_js_context_t ctx,
                                     neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->typeof_fn(ctx, variable);
}

neo_js_variable_t neo_js_context_to_string(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->to_string_fn(ctx, variable);
}

neo_js_variable_t neo_js_context_to_boolean(neo_js_context_t ctx,
                                            neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->to_boolean_fn(ctx, variable);
}

neo_js_variable_t neo_js_context_to_number(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->to_number_fn(ctx, variable);
}
bool neo_js_context_is_equal(neo_js_context_t ctx, neo_js_variable_t variable,
                             neo_js_variable_t another) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->is_equal_fn(ctx, variable, another);
}

bool neo_js_context_is_not_equal(neo_js_context_t ctx,
                                 neo_js_variable_t variable,
                                 neo_js_variable_t another) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->is_not_equal_fn(ctx, variable, another);
}