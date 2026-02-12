#include "neojs/runtime/object.h"
#include "neojs/core/string.h"
#include "neojs/engine/context.h"
#include "neojs/engine/string.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
#include <stdbool.h>

NEO_JS_CFUNCTION(neo_js_object_constructor) { return self; }

NEO_JS_CFUNCTION(neo_js_object_value_of) { return self; }
NEO_JS_CFUNCTION(neo_js_object_to_string) {
  if (self->value->type == NEO_JS_TYPE_NULL) {
    return neo_js_context_create_string(ctx, u"[object Null]");
  }
  if (self->value->type == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_string(ctx, u"[object Undefined]");
  }
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t to_string_tag =
      neo_js_variable_get_field(self, ctx, constant->symbol_to_string_tag);
  if (to_string_tag->value->type == NEO_JS_TYPE_STRING) {
    const uint16_t *src = ((neo_js_string_t)to_string_tag->value)->value;
    size_t len = neo_string16_length(src);
    uint16_t str[len + 16];
    uint16_t *dst = str;
    {
      const char *src = "[object ";
      while (*src) {
        *dst++ = *src++;
      }
    }
    while (*src) {
      *dst++ = *src++;
    }
    *dst++ = ']';
    *dst = 0;
    return neo_js_context_create_string(ctx, str);
  } else {
    return neo_js_context_create_string(ctx, u"[object Object]");
  }
}
void neo_initialize_js_object(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->object_class =
      neo_js_context_create_cfunction(ctx, neo_js_object_constructor, u"Object");
  constant->object_prototype = neo_js_variable_get_field(
      constant->object_class, ctx, constant->key_prototype);
  neo_js_variable_def_field(constant->object_prototype, ctx,
                            constant->key_constructor, constant->object_class,
                            true, false, true);
  NEO_JS_DEF_METHOD(ctx, constant->object_prototype, u"valueOf",
                    neo_js_object_value_of);
  NEO_JS_DEF_METHOD(ctx, constant->object_prototype, u"toString",
                    neo_js_object_to_string);
}