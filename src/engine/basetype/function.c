#include "engine/basetype/function.h"
#include "core/allocator.h"
#include "core/string.h"
#include "engine/basetype/cfunction.h"
#include "engine/basetype/object.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <string.h>
#include <wchar.h>

static const char *neo_js_function_typeof(neo_js_context_t ctx,
                                          neo_js_variable_t self) {
  return "function";
}

static neo_js_variable_t neo_js_function_get_field(neo_js_context_t ctx,
                                                   neo_js_variable_t self,
                                                   neo_js_variable_t field,
                                                   neo_js_variable_t receiver) {
  neo_js_type_t otype = neo_get_js_object_type();
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_STRING) {
    if (strcmp(neo_js_context_to_cstring(ctx, field), "name") == 0) {
      neo_js_cfunction_t cfunction =
          neo_js_value_to_cfunction(neo_js_variable_get_value(self));
      if (!cfunction->callable.name) {
        return neo_js_context_create_string(ctx, "");
      } else {
        return neo_js_context_create_string(ctx, cfunction->callable.name);
      }
    }
  }
  return otype->get_field_fn(ctx, self, field, receiver);
}

static neo_js_variable_t neo_js_function_set_field(neo_js_context_t ctx,
                                                   neo_js_variable_t self,
                                                   neo_js_variable_t field,
                                                   neo_js_variable_t value,
                                                   neo_js_variable_t receiver) {
  neo_js_type_t otype = neo_get_js_object_type();
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_STRING) {
    if (strcmp(neo_js_context_to_cstring(ctx, field), "name") == 0) {
      return neo_js_context_create_undefined(ctx);
    }
  }
  return otype->set_field_fn(ctx, self, field, value, receiver);
}

static neo_js_variable_t neo_js_function_del_field(neo_js_context_t ctx,
                                                   neo_js_variable_t self,
                                                   neo_js_variable_t field) {
  neo_js_type_t otype = neo_get_js_object_type();
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_STRING) {
    if (strcmp(neo_js_context_to_cstring(ctx, field), "name") == 0) {
      neo_js_function_t cfunction =
          neo_js_value_to_function(neo_js_variable_get_value(self));
      neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
      neo_allocator_free(allocator, cfunction->callable.name);
      cfunction->callable.name = neo_create_string(allocator, "");
      return neo_js_context_create_boolean(ctx, true);
    }
  }
  return otype->del_field_fn(ctx, self, field);
}

neo_js_type_t neo_get_js_function_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_JS_TYPE_FUNCTION;
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

void neo_js_function_dispose(neo_allocator_t allocator,
                             neo_js_function_t self) {
  neo_js_callable_dispose(allocator, &self->callable);
}

neo_js_function_t neo_create_js_function(neo_allocator_t allocator,
                                         neo_program_t program) {
  neo_js_function_t func = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_function_t), neo_js_function_dispose);
  neo_js_callable_init(allocator, &func->callable);
  func->callable.object.value.type = neo_get_js_function_type();
  func->address = 0;
  func->program = program;
  func->source = NULL;
  func->is_async = false;
  func->is_generator = false;
  return func;
}

neo_js_function_t neo_js_value_to_function(neo_js_value_t value) {
  if (value->type->kind == NEO_JS_TYPE_FUNCTION) {
    return (neo_js_function_t)value;
  }
  return NULL;
}