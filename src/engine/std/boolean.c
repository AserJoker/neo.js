#include "engine/std/boolean.h"
#include "engine/basetype/object.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
neo_js_variable_t neo_js_boolean_constructor(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {
  neo_js_variable_t primitive = NULL;
  if (argc) {
    primitive = neo_js_context_to_boolean(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(primitive);
  } else {
    primitive = neo_js_context_create_boolean(ctx, false);
  }
  if (neo_js_context_get_call_type(ctx) == NEO_JS_CONSTRUCT_CALL) {
    neo_js_context_set_internal(ctx, self, L"[[primitive]]", primitive);
    return neo_js_context_create_undefined(ctx);
  } else {
    return primitive;
  }
}

neo_js_variable_t neo_js_boolean_to_string(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Boolean.prototype.toString requires that 'this' be a Boolean");
  }
  neo_js_object_t obj = neo_js_variable_to_object(self);
  if (obj->constructor !=
      neo_js_variable_getneo_create_js_chunk(
          neo_js_context_get_std(ctx).boolean_constructor)) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Boolean.prototype.toString requires that 'this' be a Boolean");
  }
  neo_js_variable_t primitive =
      neo_js_context_get_internal(ctx, self, L"[[primitive]]");
  return neo_js_context_to_string(ctx, primitive);
}

neo_js_variable_t neo_js_boolean_value_of(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Boolean.prototype.valueOf requires that 'this' be a Boolean");
  }
  neo_js_object_t obj = neo_js_variable_to_object(self);
  if (obj->constructor !=
      neo_js_variable_getneo_create_js_chunk(
          neo_js_context_get_std(ctx).boolean_constructor)) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Boolean.prototype.valueOf requires that 'this' be a Boolean");
  }
  return neo_js_context_get_internal(ctx, self, L"[[primitive]]");
}