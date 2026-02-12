#include "neojs/runtime/error.h"
#include "neojs/core/allocator.h"
#include "neojs/core/list.h"
#include "neojs/core/string.h"
#include "neojs/engine/context.h"
#include "neojs/engine/runtime.h"
#include "neojs/engine/stackframe.h"
#include "neojs/engine/string.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
#include <stdbool.h>
#include <stdint.h>

NEO_JS_CFUNCTION(neo_js_error_constructor) {
  neo_js_variable_t message = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (message->value->type != NEO_JS_TYPE_STRING) {
    if (message->value->type == NEO_JS_TYPE_UNDEFINED) {
      message = neo_js_context_create_string(ctx, u"");
    } else {
      message = neo_js_variable_to_string(message, ctx);
      if (message->value->type == NEO_JS_TYPE_EXCEPTION) {
        return message;
      }
    }
  }
  neo_js_variable_t option = neo_js_context_get_argument(ctx, argc, argv, 1);
  neo_js_variable_t cause = NULL;
  neo_js_variable_t key = neo_js_context_create_string(ctx, u"cause");
  if (option->value->type >= NEO_JS_TYPE_OBJECT) {
    cause = neo_js_variable_get_field(option, ctx, key);
    if (cause->value->type == NEO_JS_TYPE_EXCEPTION) {
      return cause;
    }
  } else {
    cause = neo_js_context_get_undefined(ctx);
  }
  neo_js_variable_def_field(self, ctx, key, cause, true, false, true);
  key = neo_js_context_create_string(ctx, u"message");
  neo_js_variable_def_field(self, ctx, key, message, true, false, true);
  neo_list_t trace = neo_js_context_trace(ctx, NULL, 0, 0);
  size_t length = 16;
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  uint16_t *tracestring =
      neo_allocator_alloc(allocator, length * sizeof(uint16_t), NULL);
  *tracestring = 0;
  uint16_t prefix[] = {' ', ' ', 'a', 't', ' ', 0};
  uint16_t stuffix[] = {'\n', 0};
  for (neo_list_node_t it = neo_list_get_last(trace);
       it != neo_list_get_head(trace); it = neo_list_node_last(it)) {
    if (it != neo_list_get_last(trace)) {
      tracestring =
          neo_string16_concat(allocator, tracestring, &length, stuffix);
    }
    neo_js_stackframe_t frame = neo_list_node_get(it);
    uint16_t *s = neo_js_stackframe_to_string(allocator, frame);
    tracestring = neo_string16_concat(allocator, tracestring, &length, prefix);
    tracestring = neo_string16_concat(allocator, tracestring, &length, s);
    neo_allocator_free(allocator, s);
  }
  neo_js_variable_t stack = neo_js_context_create_string(ctx, tracestring);
  neo_allocator_free(allocator, tracestring);
  neo_allocator_free(allocator, trace);
  neo_js_variable_set_internal(self, ctx, "stack", stack);
  return self;
}
NEO_JS_CFUNCTION(neo_js_error_to_string) {
  neo_js_variable_t key = neo_js_context_create_string(ctx, u"name");
  neo_js_variable_t name = neo_js_variable_get_field(self, ctx, key);
  key = neo_js_context_create_string(ctx, u"message");
  neo_js_variable_t message = neo_js_variable_get_field(self, ctx, key);
  neo_js_variable_t stack = neo_js_variable_get_internal(self, ctx, "stack");
  const uint16_t *errname = ((neo_js_string_t)name->value)->value;
  size_t len = 0;
  len += neo_string16_length(errname) + 2;
  const uint16_t *msg = ((neo_js_string_t)message->value)->value;
  len += neo_string16_length(msg) + 1;
  const uint16_t *stackstr = ((neo_js_string_t)stack->value)->value;
  len += neo_string16_length(stackstr) + 1;
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
  if (*stackstr) {
    *dst++ = '\n';
    src = stackstr;
    while (*src) {
      *dst++ = *src++;
    }
  }
  *dst++ = 0;
  return neo_js_context_create_string(ctx, string);
}
void neo_initialize_js_error(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->error_class =
      neo_js_context_create_cfunction(ctx, neo_js_error_constructor, u"Error");
  neo_js_variable_t error_prototype = neo_js_variable_get_field(
      constant->error_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, error_prototype, u"toString", neo_js_error_to_string);
  neo_js_variable_t string = neo_js_context_create_string(ctx, u"Error");
  neo_js_variable_t key = neo_js_context_create_string(ctx, u"name");
  neo_js_variable_def_field(error_prototype, ctx, key, string, true, false,
                            true);
}