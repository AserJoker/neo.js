#include "js/context.h"
#include "core/allocator.h"
#include "core/list.h"
#include "js/basetype/boolean.h"
#include "js/basetype/error.h"
#include "js/basetype/function.h"
#include "js/basetype/null.h"
#include "js/basetype/number.h"
#include "js/basetype/object.h"
#include "js/basetype/string.h"
#include "js/basetype/symbol.h"
#include "js/basetype/undefined.h"
#include "js/handle.h"
#include "js/runtime.h"
#include "js/scope.h"
#include "js/stackframe.h"
#include "js/std/function.h"
#include "js/std/object.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"
#include <string.h>
#include <wchar.h>

struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t scope;
  neo_js_scope_t root;
  neo_list_t stacktrace;
  struct {
    neo_js_variable_t global;
    neo_js_variable_t object_constructor;
    neo_js_variable_t function_constructor;
    neo_js_variable_t number_constructor;
    neo_js_variable_t boolean_constructor;
    neo_js_variable_t string_constructor;
    neo_js_variable_t symbol_constructor;
    neo_js_variable_t array_constructor;
  } std;
};

static void neo_js_context_ininitialize_std(neo_js_context_t ctx) {
  ctx->std.object_constructor = neo_js_context_create_cfunction(
      ctx, L"Object", &neo_js_object_constructor);
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.object_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));
  neo_js_context_set_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"constructor"),
                           ctx->std.object_constructor);
  ctx->std.function_constructor = neo_js_context_create_cfunction(
      ctx, L"Function", &neo_js_function_constructor);
  neo_js_context_set_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, L"constructor"),
                           ctx->std.function_constructor);
  ctx->std.global = neo_js_context_create_object(ctx, NULL);
  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"global"),
                           ctx->std.global);
  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Function"),
                           ctx->std.function_constructor);
  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Object"),
                           ctx->std.object_constructor);
}

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t ctx) {
  neo_allocator_free(allocator, ctx->root);
  ctx->scope = NULL;
  ctx->root = NULL;
  ctx->runtime = NULL;
  memset(&ctx->std, 0, sizeof(ctx->std));
  neo_allocator_free(allocator, ctx->stacktrace);
}

neo_js_context_t neo_create_js_context(neo_allocator_t allocator,
                                       neo_js_runtime_t runtime) {
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root = neo_create_js_scope(allocator, NULL);
  ctx->scope = ctx->root;
  memset(&ctx->std, 0, sizeof(ctx->std));
  neo_list_initialize_t initialize = {true};
  ctx->stacktrace = neo_create_list(allocator, &initialize);
  neo_js_context_ininitialize_std(ctx);
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
  return ctx->std.global;
}

neo_js_scope_t neo_js_context_push_scope(neo_js_context_t ctx) {
  ctx->scope = neo_create_js_scope(neo_js_runtime_get_allocator(ctx->runtime),
                                   ctx->scope);
  return ctx->scope;
}

neo_js_scope_t neo_js_context_pop_scope(neo_js_context_t ctx) {
  neo_js_scope_t current = ctx->scope;
  ctx->scope = neo_js_scope_get_parent(ctx->scope);
  if (!neo_js_scope_release(current)) {
    neo_allocator_free(neo_js_runtime_get_allocator(ctx->runtime), current);
  }
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

neo_js_variable_t neo_js_context_get_object_constructor(neo_js_context_t ctx) {
  return ctx->std.object_constructor;
}

neo_js_variable_t
neo_js_context_get_function_constructor(neo_js_context_t ctx) {
  return ctx->std.function_constructor;
}

neo_js_variable_t neo_js_context_get_string_constructor(neo_js_context_t ctx) {
  return ctx->std.string_constructor;
}

neo_js_variable_t neo_js_context_get_boolean_constructor(neo_js_context_t ctx) {
  return ctx->std.boolean_constructor;
}

neo_js_variable_t neo_js_context_get_number_constructor(neo_js_context_t ctx) {
  return ctx->std.number_constructor;
}

neo_js_variable_t neo_js_context_get_symbol_constructor(neo_js_context_t ctx) {
  return ctx->std.symbol_constructor;
}

neo_js_variable_t neo_js_context_get_array_constructor(neo_js_context_t ctx) {
  return ctx->std.array_constructor;
}

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t ctx,
                                                 neo_js_handle_t handle) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  return neo_js_scope_create_variable(allocator, ctx->scope, handle);
}

neo_js_variable_t neo_js_context_to_primitive(neo_js_context_t ctx,
                                              neo_js_variable_t variable) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_value_t value = neo_js_variable_get_value(variable);
  neo_js_variable_t result = value->type->to_primitive_fn(ctx, variable);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_object(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_value_t value = neo_js_variable_get_value(variable);
  neo_js_variable_t result = value->type->to_object_fn(ctx, variable);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->get_field_fn(ctx, object, field);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_set_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field,
                                           neo_js_variable_t value) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->set_field_fn(ctx, object, field, value);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_del_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->del_field_fn(ctx, object, field);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_clone(neo_js_context_t ctx,
                                       neo_js_variable_t self) {
  neo_js_variable_t variable = neo_js_context_create_undefined(ctx);
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(self);
  type->copy_fn(ctx, self, variable);
  neo_js_context_pop_scope(ctx);
  return variable;
}

neo_js_variable_t neo_js_context_assigment(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           neo_js_variable_t target) {
  neo_js_variable_t variable = neo_js_context_create_undefined(ctx);
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(self);
  type->copy_fn(ctx, self, target);
  neo_js_context_pop_scope(ctx);
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_call(neo_js_context_t ctx,
                                      neo_js_variable_t callee,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  neo_js_value_t value = neo_js_variable_get_value(callee);
  if (value->type->kind < NEO_TYPE_FUNCTION) {
    return neo_js_context_create_error(ctx, L"TypeError",
                                       L"Callee is not a function");
  }
  neo_js_function_t function = neo_js_value_to_function(value);
  neo_js_scope_t current = neo_js_context_get_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_context_push_scope(ctx);
  neo_js_scope_t scope = neo_js_context_get_scope(ctx);
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t arg = argv[idx];
    neo_js_handle_t harg = neo_js_variable_get_handle(arg);
    neo_js_handle_add_parent(harg, neo_js_scope_get_root_handle(scope));
  }
  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const wchar_t *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_variable_t variable = neo_js_context_create_variable(ctx, hvalue);
    neo_js_scope_set_variable(neo_js_runtime_get_allocator(ctx->runtime), scope,
                              variable, name);
  }
  neo_js_variable_t result = function->callee(ctx, self, argc, argv);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_construct(neo_js_context_t ctx,
                                           neo_js_variable_t constructor,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(constructor)->kind != NEO_TYPE_FUNCTION) {
    return neo_js_context_create_error(ctx, L"TypeError",
                                       L"Constructor is not a function");
  }
  neo_js_scope_t current_scope = neo_js_context_get_scope(ctx);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"prototype"));
  neo_js_variable_t object = neo_js_context_create_object(ctx, prototype);
  neo_js_object_t obj_value =
      neo_js_value_to_object(neo_js_variable_get_value(object));
  neo_js_variable_t error = neo_js_context_set_field(
      ctx, object, neo_js_context_create_string(ctx, L"constructor"),
      constructor);
  if (neo_js_variable_get_type(error)->kind != NEO_TYPE_UNDEFINED) {
    object = error;
  } else {
    neo_js_variable_t result =
        neo_js_context_call(ctx, constructor, object, argc, argv);
    if (result) {
      if (neo_js_variable_get_type(result)->kind >= NEO_TYPE_OBJECT) {
        object = result;
      }
    }
  }
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  neo_js_handle_add_parent(hobject,
                           neo_js_scope_get_root_handle(current_scope));
  neo_js_context_pop_scope(ctx);
  return object;
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
neo_js_variable_t neo_js_context_create_null(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_null_t null = neo_create_js_null(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &null->value));
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
  if (!prototype) {
    if (ctx->std.object_constructor) {
      prototype = neo_js_context_get_field(
          ctx, ctx->std.object_constructor,
          neo_js_context_create_string(ctx, L"prototype"));
    } else {
      prototype = neo_js_context_create_null(ctx);
    }
  }
  neo_js_handle_t hproto = neo_js_variable_get_handle(prototype);
  neo_js_handle_add_parent(hproto, hobject);
  object->prototype = hproto;
  neo_js_variable_t result = neo_js_context_create_variable(ctx, hobject);
  if (ctx->std.object_constructor) {
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"constructor"),
                             ctx->std.object_constructor);
  }
  return result;
}

neo_js_variable_t
neo_js_context_create_cfunction(neo_js_context_t ctx, const wchar_t *name,
                                neo_js_cfunction_fn_t function) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, function);
  neo_js_handle_t hfunction =
      neo_create_js_handle(allocator, &func->callable.object.value);
  neo_js_handle_t hproto = NULL;
  if (ctx->std.function_constructor) {
    hproto = neo_js_variable_get_handle(neo_js_context_get_field(
        ctx, ctx->std.function_constructor,
        neo_js_context_create_string(ctx, L"prototype")));
  } else {
    hproto = neo_js_variable_get_handle(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_handle_add_parent(hproto, hfunction);
  if (name) {
    neo_js_handle_t hname =
        neo_js_variable_get_handle(neo_js_context_create_string(ctx, name));
    func->callable.name = hname;
    neo_js_handle_add_parent(hname, hfunction);
  }
  neo_js_variable_t result = neo_js_context_create_variable(ctx, hfunction);
  if (ctx->std.function_constructor) {
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"constructor"),
                             ctx->std.function_constructor);
  }
  neo_js_context_set_field(ctx, result,
                           neo_js_context_create_string(ctx, L"prototype"),
                           neo_js_context_create_object(ctx, NULL));
  return result;
}

const wchar_t *neo_js_context_typeof(neo_js_context_t ctx,
                                     neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->typeof_fn(ctx, variable);
}

neo_js_variable_t neo_js_context_to_string(neo_js_context_t ctx,
                                           neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_string_fn(ctx, self);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_boolean(neo_js_context_t ctx,
                                            neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_boolean_fn(ctx, self);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_number(neo_js_context_t ctx,
                                           neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_number_fn(ctx, self);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult);
  neo_js_context_pop_scope(ctx);
  return result;
}
bool neo_js_context_is_equal(neo_js_context_t ctx, neo_js_variable_t variable,
                             neo_js_variable_t another) {
  neo_js_type_t lefttype = neo_js_variable_get_type(variable);
  neo_js_type_t righttype = neo_js_variable_get_type(another);
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  if (lefttype->kind != righttype->kind) {
    if (lefttype->kind == NEO_TYPE_NULL ||
        lefttype->kind == NEO_TYPE_UNDEFINED) {
      if (righttype->kind == NEO_TYPE_NULL ||
          righttype->kind == NEO_TYPE_UNDEFINED) {
        return true;
      }
    }
    if (righttype->kind == NEO_TYPE_NULL ||
        righttype->kind == NEO_TYPE_UNDEFINED) {
      if (righttype->kind == NEO_TYPE_NULL ||
          righttype->kind == NEO_TYPE_UNDEFINED) {
        return true;
      }
    }
    if (lefttype->kind >= NEO_TYPE_OBJECT &&
        righttype->kind < NEO_TYPE_OBJECT) {
      left = neo_js_context_to_primitive(ctx, left);
      lefttype = neo_js_variable_get_type(left);
    }
    if (righttype->kind >= NEO_TYPE_OBJECT &&
        lefttype->kind < NEO_TYPE_OBJECT) {
      right = neo_js_context_to_primitive(ctx, right);
      righttype = neo_js_variable_get_type(right);
    }
    if (lefttype->kind != righttype->kind) {
      if (lefttype->kind == NEO_TYPE_SYMBOL ||
          righttype->kind == NEO_TYPE_SYMBOL) {
        return false;
      }
    }
    if (lefttype->kind == NEO_TYPE_BOOLEAN) {
      left = neo_js_context_to_number(ctx, left);
      return neo_js_context_is_equal(ctx, left, right);
    }
    if (righttype->kind == NEO_TYPE_BOOLEAN) {
      right = neo_js_context_to_number(ctx, right);
      return neo_js_context_is_equal(ctx, left, right);
    }
    if (lefttype->kind == NEO_TYPE_STRING &&
        righttype->kind == NEO_TYPE_NUMBER) {
      left = neo_js_context_to_number(ctx, left);
      lefttype = neo_js_variable_get_type(left);
    } else if (lefttype->kind == NEO_TYPE_NUMBER &&
               righttype->kind == NEO_TYPE_STRING) {
      right = neo_js_context_to_number(ctx, right);
      righttype = neo_js_variable_get_type(right);
    }
  }
  return lefttype->is_equal_fn(ctx, left, right);
}

bool neo_js_context_is_not_equal(neo_js_context_t ctx,
                                 neo_js_variable_t variable,
                                 neo_js_variable_t another) {
  return !neo_js_context_is_equal(ctx, variable, another);
}