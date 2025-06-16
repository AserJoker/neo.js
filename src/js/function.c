#include "js/function.h"
#include "core/allocator.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "js/context.h"
#include "js/handle.h"
#include "js/object.h"
#include "js/runtime.h"
#include "js/string.h"
#include "js/type.h"
#include "js/variable.h"
#include <stdbool.h>
#include <wchar.h>

static const wchar_t *neo_js_function_typeof() { return L"function"; }

static neo_js_variable_t neo_js_function_to_string(neo_js_context_t ctx,
                                                   neo_js_variable_t self) {
  neo_js_function_t function =
      neo_js_value_to_function(neo_js_variable_get_value(self));
  if (function->source) {
    return neo_js_context_create_string(ctx, function->source);
  }
  if (function->name) {
    const wchar_t *name =
        neo_js_value_to_string(neo_js_handle_get_value(function->name))->string;
    neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
    neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
    wchar_t *source = neo_allocator_alloc(allocator, wcslen(name) + 32, NULL);
    swprintf(source, wcslen(name) + 32, L"function %ls(){ [native code] }",
             name);
    neo_js_variable_t string =
        neo_js_context_create_string(ctx, L"function (){ [native code] }");
    neo_allocator_free(allocator, source);
    return string;
  }
  return neo_js_context_create_string(ctx, L"function (){ [native code] }");
}

neo_js_type_t neo_get_js_function_type() {
  static struct _neo_js_type_t type = {0};
  neo_js_type_t otype = neo_get_js_object_type();
  type.typeof_fn = neo_js_function_typeof;
  type.to_boolean_fn = otype->to_boolean_fn;
  type.to_number_fn = otype->to_number_fn;
  type.to_string_fn = otype->to_string_fn;
  type.to_primitive_fn = otype->to_primitive_fn;
  type.to_object_fn = otype->to_object_fn;
  type.get_field_fn = otype->get_field_fn;
  type.set_field_fn = otype->set_field_fn;
  type.del_field_fn = otype->del_field_fn;
  type.is_equal_fn = otype->is_equal_fn;
  return &type;
}

static void neo_js_function_dispose(neo_allocator_t allocator,
                                    neo_js_function_t self) {
  neo_allocator_free(allocator, self->closure);
  neo_allocator_free(allocator, self->object.properties);
}

neo_js_function_t neo_create_js_function(neo_allocator_t allocator,
                                         neo_js_cfunction_fn_t callee) {
  neo_js_function_t function = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_function_t), neo_js_function_dispose);
  function->callee = callee;
  function->name = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = true;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  initialize.compare = (neo_compare_fn_t)wcscmp;
  function->closure = neo_create_hash_map(allocator, &initialize);

  function->object.value.type = neo_get_js_function_type();
  function->object.value.ref = 0;
  initialize.auto_free_key = false;
  initialize.auto_free_value = false;
  initialize.compare = (neo_compare_fn_t)neo_js_object_compare_key;
  initialize.hash = (neo_hash_fn_t)neo_js_object_key_hash;
  function->object.properties = neo_create_hash_map(allocator, &initialize);
  function->source = NULL;
  return function;
}

neo_js_function_t neo_js_value_to_function(neo_js_value_t value) {
  if (value->type == neo_get_js_function_type()) {
    return (neo_js_function_t)value;
  }
  return NULL;
}