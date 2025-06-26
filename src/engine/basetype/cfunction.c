#include "engine/basetype/cfunction.h"
#include "core/allocator.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "engine/basetype/object.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <wchar.h>

static const wchar_t *neo_js_function_typeof() { return L"cfunction"; }

static neo_js_variable_t neo_js_function_to_string(neo_js_context_t ctx,
                                                   neo_js_variable_t self) {
  neo_js_cfunction_t cfunction =
      neo_js_value_to_cfunction(neo_js_variable_get_value(self));
  if (cfunction->callable.source) {
    return neo_js_context_create_string(ctx, cfunction->callable.source);
  }
  if (cfunction->callable.name) {
    const wchar_t *name = neo_js_value_to_string(
                              neo_js_handle_get_value(cfunction->callable.name))
                              ->string;
    neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
    neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
    wchar_t *source = neo_allocator_alloc(allocator, wcslen(name) + 32, NULL);
    swprintf(source, wcslen(name) + 32, L"cfunction %ls(){ [native code] }",
             name);
    neo_js_variable_t string =
        neo_js_context_create_string(ctx, L"cfunction (){ [native code] }");
    neo_allocator_free(allocator, source);
    return string;
  }
  return neo_js_context_create_string(ctx, L"cfunction (){ [native code] }");
}
static neo_js_variable_t neo_js_function_get_field(neo_js_context_t ctx,
                                                   neo_js_variable_t self,
                                                   neo_js_variable_t field) {
  neo_js_type_t otype = neo_get_js_object_type();
  if (neo_js_variable_get_type(field)->kind == NEO_TYPE_STRING) {
    neo_js_string_t string =
        neo_js_value_to_string(neo_js_variable_get_value(field));
    if (wcscmp(string->string, L"name") == 0) {
      neo_js_cfunction_t cfunction =
          neo_js_value_to_cfunction(neo_js_variable_get_value(self));
      if (!cfunction->callable.name) {
        return neo_js_context_create_string(ctx, L"");
      } else {
        return neo_js_context_create_variable(ctx, cfunction->callable.name);
      }
    }
  }
  return otype->get_field_fn(ctx, self, field);
}

static neo_js_variable_t neo_js_function_set_field(neo_js_context_t ctx,
                                                   neo_js_variable_t self,
                                                   neo_js_variable_t field,
                                                   neo_js_variable_t value) {
  neo_js_type_t otype = neo_get_js_object_type();
  if (neo_js_variable_get_type(field)->kind == NEO_TYPE_STRING) {
    neo_js_string_t string =
        neo_js_value_to_string(neo_js_variable_get_value(field));
    if (wcscmp(string->string, L"name") == 0) {
      return NULL;
    }
  }
  return otype->set_field_fn(ctx, self, field, value);
}

static neo_js_variable_t neo_js_function_del_field(neo_js_context_t ctx,
                                                   neo_js_variable_t self,
                                                   neo_js_variable_t field) {
  neo_js_type_t otype = neo_get_js_object_type();
  if (neo_js_variable_get_type(field)->kind == NEO_TYPE_STRING) {
    neo_js_string_t string =
        neo_js_value_to_string(neo_js_variable_get_value(field));
    if (wcscmp(string->string, L"name") == 0) {
      neo_js_cfunction_t cfunction =
          neo_js_value_to_cfunction(neo_js_variable_get_value(self));
      if (cfunction->callable.name) {
        neo_js_scope_t scope = neo_js_context_get_scope(ctx);
        neo_js_handle_t root = neo_js_scope_get_root_handle(scope);
        neo_js_handle_add_parent(cfunction->callable.name, root);
        neo_js_handle_remove_parent(cfunction->callable.name,
                                    neo_js_variable_get_handle(self));
        cfunction->callable.name = NULL;
      }
      return neo_js_context_create_boolean(ctx, true);
    }
  }
  return otype->del_field_fn(ctx, self, field);
}

neo_js_type_t neo_get_js_cfunction_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_TYPE_CFUNCTION;
  neo_js_type_t otype = neo_get_js_object_type();
  type.typeof_fn = neo_js_function_typeof;
  type.to_boolean_fn = otype->to_boolean_fn;
  type.to_number_fn = otype->to_number_fn;
  type.to_string_fn = otype->to_string_fn;
  type.to_primitive_fn = otype->to_primitive_fn;
  type.to_object_fn = otype->to_object_fn;
  type.get_field_fn = neo_js_function_get_field;
  type.set_field_fn = neo_js_function_set_field;
  type.del_field_fn = neo_js_function_del_field;
  type.is_equal_fn = otype->is_equal_fn;
  type.copy_fn = otype->copy_fn;
  return &type;
}

static void neo_js_function_dispose(neo_allocator_t allocator,
                                    neo_js_cfunction_t self) {
  neo_allocator_free(allocator, self->callable.closure);
  neo_allocator_free(allocator, self->callable.object.properties);
  neo_allocator_free(allocator, self->callable.object.internal);
}

neo_js_cfunction_t neo_create_js_cfunction(neo_allocator_t allocator,
                                           neo_js_cfunction_fn_t callee) {
  neo_js_cfunction_t cfunction = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_cfunction_t), neo_js_function_dispose);
  cfunction->callee = callee;
  cfunction->callable.name = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = true;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  initialize.compare = (neo_compare_fn_t)wcscmp;
  cfunction->callable.closure = neo_create_hash_map(allocator, &initialize);

  cfunction->callable.object.value.type = neo_get_js_cfunction_type();
  cfunction->callable.object.value.ref = 0;
  cfunction->callable.object.extensible = true;
  cfunction->callable.object.frozen = false;
  cfunction->callable.object.sealed = false;
  initialize.auto_free_key = false;
  initialize.auto_free_value = true;
  initialize.compare = (neo_compare_fn_t)neo_js_object_compare_key;
  initialize.hash = (neo_hash_fn_t)neo_js_object_key_hash;
  cfunction->callable.object.properties =
      neo_create_hash_map(allocator, &initialize);

  initialize.auto_free_key = true;
  initialize.auto_free_value = true;
  initialize.compare = (neo_compare_fn_t)wcscmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  cfunction->callable.object.internal =
      neo_create_hash_map(allocator, &initialize);
  cfunction->callable.source = NULL;
  cfunction->callable.object.constructor = NULL;
  return cfunction;
}

neo_js_cfunction_t neo_js_value_to_cfunction(neo_js_value_t value) {
  if (value->type->kind >= NEO_TYPE_CFUNCTION) {
    return (neo_js_cfunction_t)value;
  }
  return NULL;
}