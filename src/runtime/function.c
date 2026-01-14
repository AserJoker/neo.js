#include "neo.js/runtime/function.h"
#include "neo.js/engine/callable.h"
#include "neo.js/engine/context.h"
#include "neo.js/engine/function.h"
#include "neo.js/engine/string.h"
#include "neo.js/engine/value.h"
#include "neo.js/engine/variable.h"
#include "neo.js/runtime/constant.h"
#include "neo.js/runtime/object.h"
#include <stdbool.h>
#include <stdint.h>

NEO_JS_CFUNCTION(neo_js_function_constructor) { return self; }
NEO_JS_CFUNCTION(neo_js_function_to_string) {
  if (self->value->type == NEO_JS_TYPE_FUNCTION) {
    neo_js_callable_t callable = (neo_js_callable_t)self->value;
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t name =
        neo_js_variable_get_field(self, ctx, constant->key_name);
    const uint16_t *funcname = ((neo_js_string_t)name->value)->value;
    if (callable->is_native) {
      if (*funcname) {
        return neo_js_context_format(ctx, "function %v(){[native]}", name);
      } else {
        return neo_js_context_create_cstring(ctx,
                                             "function anonymous(){[native]}");
      }
    } else {
      neo_js_function_t func = (neo_js_function_t)callable;
      return neo_js_context_create_cstring(ctx, func->source);
    }
  }
  return neo_js_object_to_string(ctx, self, argc, argv);
}
void neo_initialize_js_function(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->function_class = neo_js_context_create_cfunction(
      ctx, neo_js_function_constructor, "Object");
  constant->function_prototype = neo_js_variable_get_field(
      constant->function_class, ctx, constant->key_prototype);
  neo_js_variable_def_field(constant->function_prototype, ctx,
                            constant->key_constructor, constant->function_class,
                            true, false, true);
  // fix object class prototype
  {
    neo_js_variable_set_prototype_of(constant->object_class, ctx,
                                     constant->function_prototype);
  }
  NEO_JS_DEF_METHOD(ctx, constant->function_prototype, "toString",
                    neo_js_function_to_string);
}