#include "neojs/runtime/boolean.h"
#include "neojs/engine/context.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
NEO_JS_CFUNCTION(neo_js_boolean_constructor) {
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  value = neo_js_variable_to_boolean(value, ctx);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value;
  }
  if (neo_js_context_get_type(ctx) == NEO_JS_CONTEXT_CONSTRUCT) {
    neo_js_variable_set_internal(self, ctx, "value", value);
    return self;
  } else {
    return value;
  }
}
NEO_JS_CFUNCTION(neo_js_boolean_to_string) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  if (!value || value->value->type != NEO_JS_TYPE_BOOLEAN) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Boolean.prototype.toString requires that 'this' be a Boolean");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  return neo_js_variable_to_boolean(value, ctx);
}
NEO_JS_CFUNCTION(neo_js_boolean_value_of) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  if (!value || value->value->type != NEO_JS_TYPE_BOOLEAN) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Boolean.prototype.valueOf requires that 'this' be a Boolean");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  return value;
}
void neo_initialize_js_boolean(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->boolean_class = neo_js_context_create_cfunction(
      ctx, neo_js_boolean_constructor, "Boolean");
  neo_js_variable_t prototype = neo_js_variable_get_field(
      constant->boolean_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, prototype, "toString", neo_js_boolean_to_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "valueOf", neo_js_boolean_value_of);
}