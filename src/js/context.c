#include "js/context.h"
#include "core/allocator.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "core/string.h"
#include "js/basetype/array.h"
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
#include "js/std/array.h"
#include "js/std/function.h"
#include "js/std/object.h"
#include "js/std/symbol.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"
#include <stdbool.h>
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
    neo_js_variable_t async_function_constructor;
    neo_js_variable_t number_constructor;
    neo_js_variable_t boolean_constructor;
    neo_js_variable_t string_constructor;
    neo_js_variable_t symbol_constructor;
    neo_js_variable_t array_constructor;
    neo_js_variable_t bigint_constructor;
    neo_js_variable_t regexp_constructor;
    neo_js_variable_t iterator_constructor;
    neo_js_variable_t array_iterator_constructor;
    neo_js_variable_t generator_constructor;
    neo_js_variable_t generator_iterator_constructor;
    neo_js_variable_t async_generator_constructor;
    neo_js_variable_t async_generator_iterator_constructor;
  } std;
};

static void neo_js_context_init_std_symbol(neo_js_context_t ctx) {
  neo_js_context_push_scope(ctx);
  neo_js_variable_t async_iterator =
      neo_js_context_create_symbol(ctx, L"asyncIterator");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"asyncIterator"),
                           async_iterator, true, false, true);

  neo_js_variable_t has_instance =
      neo_js_context_create_symbol(ctx, L"hasInstance");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"hasInstance"),
                           has_instance, true, false, true);

  neo_js_variable_t is_concat_spreadable =
      neo_js_context_create_symbol(ctx, L"isConcatSpreadable");
  neo_js_context_def_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"isConcatSpreadable"),
      is_concat_spreadable, true, false, true);

  neo_js_variable_t iterator = neo_js_context_create_symbol(ctx, L"iterator");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"iterator"),
                           iterator, true, false, true);

  neo_js_variable_t match = neo_js_context_create_symbol(ctx, L"match");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"match"), match,
                           true, false, true);

  neo_js_variable_t match_all = neo_js_context_create_symbol(ctx, L"matchAll");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"matchAll"),
                           match_all, true, false, true);

  neo_js_variable_t replace = neo_js_context_create_symbol(ctx, L"replace");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"replace"),
                           replace, true, false, true);

  neo_js_variable_t search = neo_js_context_create_symbol(ctx, L"search");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"search"), search,
                           true, false, true);

  neo_js_variable_t species = neo_js_context_create_symbol(ctx, L"species");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"species"),
                           species, true, false, true);

  neo_js_variable_t split = neo_js_context_create_symbol(ctx, L"split");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"split"), split,
                           true, false, true);

  neo_js_variable_t to_primitive =
      neo_js_context_create_symbol(ctx, L"toPrimitive");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"toPrimitive"),
                           to_primitive, true, false, true);

  neo_js_variable_t to_string_tag =
      neo_js_context_create_symbol(ctx, L"toStringTag");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"toStringTag"),
                           to_string_tag, true, false, true);

  neo_js_variable_t for_ =
      neo_js_context_create_cfunction(ctx, L"for", &neo_js_symbol_for);
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"for"), for_,
                           true, false, true);

  neo_js_variable_t key_for =
      neo_js_context_create_cfunction(ctx, L"keyFor", &neo_js_symbol_key_for);
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"keyFor"),
                           key_for, true, false, true);

  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_variable_t to_string = neo_js_context_create_cfunction(
      ctx, L"toString", &neo_js_symbol_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           to_string, true, false, true);

  neo_js_variable_t value_of =
      neo_js_context_create_cfunction(ctx, L"valueOf", &neo_js_symbol_value_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"valueOf"),
                           value_of, true, false, true);

  neo_js_variable_t to_primitive_func = neo_js_context_create_cfunction(
      ctx, L"[Symbol.toPrimitive]", &neo_js_symbol_to_primitive);

  neo_js_context_def_field(ctx, prototype, to_primitive, to_primitive_func,
                           true, false, true);
  neo_js_context_pop_scope(ctx);
}

static void neo_js_context_init_std_object(neo_js_context_t ctx) {

  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"keys"),
      neo_js_context_create_cfunction(ctx, L"keys", neo_js_object_keys), true,
      false, true);

  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.object_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"valueOf"),
      neo_js_context_create_cfunction(ctx, L"valueOf", neo_js_object_value_of),
      true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           neo_js_context_create_cfunction(
                               ctx, L"toString", neo_js_object_to_string),
                           true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toLocalString"),
      neo_js_context_create_cfunction(ctx, L"toLocalString",
                                      neo_js_object_to_local_string),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"hasOwnProperty"),
      neo_js_context_create_cfunction(ctx, L"hasOwnProperty",
                                      neo_js_object_has_own_property),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"isPrototypeOf"),
      neo_js_context_create_cfunction(ctx, L"isPrototypeOf",
                                      neo_js_object_is_prototype_of),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"isPrototypeOf"),
      neo_js_context_create_cfunction(ctx, L"isPrototypeOf",
                                      neo_js_object_property_is_enumerable),
      true, false, true);
}

static void neo_js_context_init_std_function(neo_js_context_t ctx) {}

static void neo_js_context_init_std_array(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.array_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString", neo_js_array_to_string),
      true, false, true);
}

static void neo_js_context_init_std(neo_js_context_t ctx) {
  ctx->std.object_constructor = neo_js_context_create_cfunction(
      ctx, L"Object", &neo_js_object_constructor);

  neo_js_variable_t prototype =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx), NULL);

  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, L"prototype"),
                           prototype, true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"constructor"),
                           ctx->std.object_constructor, true, false, true);

  ctx->std.function_constructor = neo_js_context_create_cfunction(
      ctx, L"Function", &neo_js_function_constructor);

  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, L"constructor"),
                           ctx->std.function_constructor, true, false, true);

  neo_js_context_def_field(ctx, ctx->std.function_constructor,
                           neo_js_context_create_string(ctx, L"prototype"),
                           neo_js_context_create_object(ctx, NULL, NULL), true,
                           false, true);

  ctx->std.symbol_constructor = neo_js_context_create_cfunction(
      ctx, L"Symbol", neo_js_symbol_constructor);

  neo_js_context_init_std_symbol(ctx);

  neo_js_context_init_std_object(ctx);

  neo_js_context_init_std_function(ctx);

  ctx->std.array_constructor =
      neo_js_context_create_cfunction(ctx, L"Array", neo_js_array_constructor);

  neo_js_context_init_std_array(ctx);

  ctx->std.global = neo_js_context_create_object(ctx, NULL, NULL);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"global"),
                           ctx->std.global);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Function"),
                           ctx->std.function_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Object"),
                           ctx->std.object_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Symbol"),
                           ctx->std.symbol_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Array"),
                           ctx->std.array_constructor);
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
  neo_js_context_init_std(ctx);
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

neo_js_variable_t neo_js_context_has_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field) {
  object = neo_js_context_to_object(ctx, object);
  return neo_js_context_create_boolean(
      ctx, neo_js_object_get_property(ctx, object, field));
}

neo_js_variable_t
neo_js_context_def_field(neo_js_context_t ctx, neo_js_variable_t object,
                         neo_js_variable_t field, neo_js_variable_t value,
                         bool configurable, bool enumable, bool writable) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (neo_js_variable_get_type(object)->kind < NEO_TYPE_OBJECT) {
    return neo_js_context_create_error(
        ctx, L"TypeError", L"Object.defineProperty called on non-object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  field = neo_js_context_to_primitive(ctx, field);
  if (neo_js_variable_get_type(field)->kind != NEO_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(ctx, object, field);
  neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  if (!prop) {
    if (obj->sealed || obj->frozen || !obj->extensible) {
      return neo_js_context_create_error(ctx, L"TypeError",
                                         L"Object is not extensible");
    }
    prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumerable = enumable;
    prop->writable = writable;
    prop->value = hvalue;
    prop->get = NULL;
    prop->set = NULL;
    neo_js_handle_t hfield = neo_js_variable_get_handle(field);
    neo_hash_map_set(obj->properties, hfield, prop, ctx, ctx);
    neo_js_handle_add_parent(hvalue, hobject);
    neo_js_handle_add_parent(hfield, hobject);
  } else {
    if (obj->frozen) {
      return neo_js_context_create_error(ctx, L"TypeError",
                                         L"Object is not extensible");
    }
    if (prop->value != NULL) {
      neo_js_variable_t current =
          neo_js_context_create_variable(ctx, prop->value);
      if (neo_js_variable_get_type(current) ==
              neo_js_variable_get_type(value) &&
          neo_js_context_is_equal(ctx, current, value)) {
        return object;
      }
      if (!prop->configurable || !prop->writable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
          neo_js_symbol_t sym = neo_js_variable_to_symbol(field);
          size_t len = wcslen(sym->description) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property Symbol(%ls) of object "
                   L"'#<Object>'",
                   sym->description);
        } else {
          neo_js_string_t str = neo_js_variable_to_string(field);
          size_t len = wcslen(str->string) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property '%ls' of object "
                   L"'#<Object>'",
                   str->string);
        }
        neo_js_variable_t error =
            neo_js_context_create_error(ctx, L"TypeError", message);
        neo_allocator_free(allocator, message);
        return error;
      }
      neo_js_handle_remove_parent(prop->value, hobject);
      neo_js_handle_add_parent(prop->value,
                               neo_js_scope_get_root_handle(ctx->scope));
    } else {
      if (!prop->configurable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
          neo_js_symbol_t sym = neo_js_variable_to_symbol(field);
          size_t len = wcslen(sym->description) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len, L"Cannot redefine property: Symbol(%ls)",
                   sym->description);
        } else {
          neo_js_string_t str = neo_js_variable_to_string(field);
          size_t len = wcslen(str->string) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len, L"Cannot redefine property: '%ls'",
                   str->string);
        }
        neo_js_variable_t error =
            neo_js_context_create_error(ctx, L"TypeError", message);
        neo_allocator_free(allocator, message);
        return error;
      }
      if (prop->get) {
        neo_js_handle_add_parent(prop->get,
                                 neo_js_scope_get_root_handle(ctx->scope));
        neo_js_handle_remove_parent(prop->get, hobject);
        prop->get = NULL;
      }
      if (prop->set) {
        neo_js_handle_add_parent(prop->set,
                                 neo_js_scope_get_root_handle(ctx->scope));
        neo_js_handle_remove_parent(prop->set, hobject);
        prop->set = NULL;
      }
    }
    neo_js_handle_add_parent(hvalue, hobject);
    prop->value = hvalue;
    prop->writable = writable;
  }
  return object;
}

neo_js_variable_t
neo_js_context_def_accessor(neo_js_context_t ctx, neo_js_variable_t object,
                            neo_js_variable_t field, neo_js_variable_t getter,
                            neo_js_variable_t setter, bool configurable,
                            bool enumable) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (neo_js_variable_get_type(object)->kind < NEO_TYPE_OBJECT) {
    return neo_js_context_create_error(
        ctx, L"TypeError", L"Object.defineProperty called on non-object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  field = neo_js_context_to_primitive(ctx, field);
  if (neo_js_variable_get_type(field)->kind != NEO_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(ctx, object, field);
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  neo_js_handle_t hgetter = NULL;
  neo_js_handle_t hsetter = NULL;
  if (getter) {
    hgetter = neo_js_variable_get_handle(getter);
  }
  if (setter) {
    hsetter = neo_js_variable_get_handle(setter);
  }
  if (!prop) {
    if (obj->sealed || obj->frozen || !obj->extensible) {
      return neo_js_context_create_error(ctx, L"TypeError",
                                         L"Object is not extensible");
    }
    prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumerable = enumable;
    prop->get = hgetter;
    prop->set = hsetter;
    if (hgetter) {
      neo_js_handle_add_parent(hgetter, hobject);
    }
    if (hsetter) {
      neo_js_handle_add_parent(hsetter, hobject);
    }
  } else {
    if (prop->value != NULL) {
      if (!prop->configurable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
          neo_js_symbol_t sym = neo_js_variable_to_symbol(field);
          size_t len = wcslen(sym->description) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property Symbol(%ls) of object "
                   L"'#<Object>'",
                   sym->description);
        } else {
          neo_js_string_t str = neo_js_variable_to_string(field);
          size_t len = wcslen(str->string) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property '%ls' of object "
                   L"'#<Object>'",
                   str->string);
        }
        neo_js_variable_t error =
            neo_js_context_create_error(ctx, L"TypeError", message);
        neo_allocator_free(allocator, message);
        return error;
      }
      neo_js_handle_add_parent(prop->value,
                               neo_js_scope_get_root_handle(ctx->scope));
      neo_js_handle_remove_parent(prop->value, hobject);
      prop->value = NULL;
      prop->writable = false;
    } else {
      if (!prop->configurable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
          neo_js_symbol_t sym = neo_js_variable_to_symbol(field);
          size_t len = wcslen(sym->description) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len, L"Cannot redefine property: Symbol(%ls)",
                   sym->description);
        } else {
          neo_js_string_t str = neo_js_variable_to_string(field);
          size_t len = wcslen(str->string) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len, L"Cannot redefine property: '%ls'",
                   str->string);
        }
        neo_js_variable_t error =
            neo_js_context_create_error(ctx, L"TypeError", message);
        neo_allocator_free(allocator, message);
        return error;
      }
    }
    if (prop->get) {
      neo_js_handle_add_parent(prop->get,
                               neo_js_scope_get_root_handle(ctx->scope));
      neo_js_handle_remove_parent(prop->get, hobject);
      prop->get = NULL;
    }
    if (prop->set) {
      neo_js_handle_add_parent(prop->set,
                               neo_js_scope_get_root_handle(ctx->scope));
      neo_js_handle_remove_parent(prop->set, hobject);
      prop->set = NULL;
    }
    if (hgetter) {
      neo_js_handle_add_parent(hgetter, hobject);
      prop->get = hgetter;
    }
    if (hsetter) {
      neo_js_handle_add_parent(hsetter, hobject);
      prop->set = hsetter;
    }
  }
  return object;
}

neo_js_variable_t neo_js_context_get_internal(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              const wchar_t *field) {
  neo_js_object_t object = neo_js_variable_to_object(self);
  if (object) {
    return neo_js_context_create_variable(
        ctx, neo_hash_map_get(object->internal, field, NULL, NULL));
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_set_internal(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              const wchar_t *field,
                                              neo_js_variable_t value) {
  neo_js_object_t object = neo_js_variable_to_object(self);
  if (object) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_js_handle_t current =
        neo_hash_map_get(object->internal, field, NULL, NULL);
    if (current) {
      neo_js_handle_add_parent(current,
                               neo_js_scope_get_root_handle(ctx->scope));
    }
    neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
    neo_hash_map_set(object->internal, neo_create_wstring(allocator, field),
                     hvalue, NULL, NULL);
    neo_js_handle_add_parent(hvalue, neo_js_variable_get_handle(self));
  }
  return neo_js_context_create_undefined(ctx);
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
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_scope_t current = neo_js_context_get_scope(ctx);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"prototype"));
  neo_js_object_t obj = NULL;
  if (neo_js_variable_get_value(constructor) ==
      neo_js_variable_get_value(ctx->std.array_constructor)) {
    obj = &neo_create_js_array(allocator)->object;
  } else {
    obj = neo_create_js_object(allocator);
  }
  neo_js_variable_t object = neo_js_context_create_object(ctx, prototype, obj);
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  obj->constructor = neo_js_variable_get_handle(constructor);
  neo_js_handle_add_parent(obj->constructor, hobject);
  neo_js_context_def_field(ctx, object,
                           neo_js_context_create_string(ctx, L"constructor"),
                           constructor, true, false, true);
  neo_js_variable_t result =
      neo_js_context_call(ctx, constructor, object, argc, argv);
  if (result) {
    if (neo_js_variable_get_type(result)->kind >= NEO_TYPE_OBJECT) {
      object = result;
    }
  }
  hobject = neo_js_variable_get_handle(object);
  object = neo_js_scope_create_variable(allocator, current, hobject);
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
                                               const wchar_t *description) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_symbol_t symbol = neo_create_js_symbol(allocator, description);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &symbol->value));
}
neo_js_variable_t neo_js_context_create_object(neo_js_context_t ctx,
                                               neo_js_variable_t prototype,
                                               neo_js_object_t object) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  if (!object) {
    object = neo_create_js_object(allocator);
  }
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
    neo_js_context_def_field(ctx, result,
                             neo_js_context_create_string(ctx, L"constructor"),
                             ctx->std.object_constructor, true, false, true);
  }
  return result;
}

neo_js_variable_t neo_js_context_create_array(neo_js_context_t ctx) {
  return neo_js_context_construct(ctx, ctx->std.array_constructor, 0, NULL);
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
    neo_js_context_def_field(ctx, result,
                             neo_js_context_create_string(ctx, L"constructor"),
                             ctx->std.function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, L"prototype"),
      neo_js_context_create_object(ctx, NULL, NULL), true, false, true);
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

bool neo_js_context_instance_of(neo_js_context_t ctx,
                                neo_js_variable_t variable,
                                neo_js_variable_t constructor) {
  neo_js_type_t type = neo_js_variable_get_type(variable);
  if (type->kind != NEO_TYPE_OBJECT) {
    return false;
  }
  neo_js_variable_t hasInstance = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"hasInstance"));
  if (hasInstance &&
      neo_js_variable_get_type(hasInstance)->kind == NEO_TYPE_FUNCTION) {
    return neo_js_context_call(ctx, hasInstance, constructor, 1, &variable);
  }
  neo_js_object_t object = neo_js_variable_to_object(variable);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"prototype"));
  while (object) {
    if (object == neo_js_variable_to_object(prototype)) {
      return neo_js_context_create_boolean(ctx, true);
    }
    object = neo_js_value_to_object(neo_js_handle_get_value(object->prototype));
  }
  return neo_js_context_create_boolean(ctx, false);
}