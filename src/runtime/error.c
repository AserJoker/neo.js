#include "runtime/error.h"
#include "core/string.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "engine/stackframe.h"
#include "engine/string.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <stdbool.h>
#include <stdint.h>

NEO_JS_CFUNCTION(neo_js_error_constructor) {
  neo_js_variable_t message = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (message->value->type != NEO_JS_TYPE_STRING) {
    if (message->value->type == NEO_JS_TYPE_UNDEFINED) {
      message = neo_js_context_create_cstring(ctx, "");
    } else {
      message = neo_js_variable_to_string(message, ctx);
      if (message->value->type == NEO_JS_TYPE_EXCEPTION) {
        return message;
      }
    }
  }
  neo_js_variable_t option = neo_js_context_get_argument(ctx, argc, argv, 1);
  neo_js_variable_t cause = NULL;
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "cause");
  if (option->value->type >= NEO_JS_TYPE_OBJECT) {
    cause = neo_js_variable_get_field(option, ctx, key);
    if (cause->value->type == NEO_JS_TYPE_EXCEPTION) {
      return cause;
    }
  } else {
    cause = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_def_field(self, ctx, key, cause, true, false, true);
  key = neo_js_context_create_cstring(ctx, "message");
  neo_js_variable_def_field(self, ctx, key, message, true, false, true);
  return self;
}
NEO_JS_CFUNCTION(neo_js_error_to_string) {
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "name");
  neo_js_variable_t name = neo_js_variable_get_field(self, ctx, key);
  key = neo_js_context_create_cstring(ctx, "message");
  neo_js_variable_t message = neo_js_variable_get_field(self, ctx, key);
  const uint16_t *errname = ((neo_js_string_t)name->value)->value;
  size_t len = 0;
  len += neo_string16_length(errname) + 2;
  const uint16_t *msg = ((neo_js_string_t)message->value)->value;
  len += neo_string16_length(msg) + 1;
  uint16_t string[len];
  const uint16_t *src = errname;
  uint16_t *dst = string;
  while (*src) {
    *dst++ = *src++;
  }
  *dst++ = ':';
  *dst++ = ' ';
  src = msg;
  while (*src) {
    *dst++ = *src++;
  }
  *dst++ = 0;
  return neo_js_context_create_string(ctx, string);
}
void neo_initialize_js_error(neo_js_context_t ctx) {
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  constant->error_class =
      neo_js_context_create_cfunction(ctx, neo_js_error_constructor, "Error");
  neo_js_variable_t error_prototype = neo_js_variable_get_field(
      constant->error_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, error_prototype, "toString", neo_js_error_to_string);
  neo_js_variable_t string = neo_js_context_create_cstring(ctx, "Error");
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "name");
  neo_js_variable_def_field(error_prototype, ctx, key, string, true, false,
                            true);
  neo_js_scope_set_variable(root_scope, constant->error_class, NULL);
}