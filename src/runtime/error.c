#include "runtime/error.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/string.h"
#include "engine/context.h"
#include "engine/runtime.h"
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
  neo_list_t trace = neo_js_context_trace(ctx, 0, 0);
  neo_list_pop(trace);
  neo_list_node_t it = neo_list_get_first(trace);
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_list_initialize_t initialize = {true};
  neo_list_t list = neo_create_list(allocator, &initialize);
  size_t len = 0;
  while (it != neo_list_get_tail(trace)) {
    neo_js_stackframe_t frame = neo_list_node_get(it);
    uint16_t *string = neo_js_stackframe_to_string(allocator, frame);
    len += neo_string16_length(string) + 8;
    neo_list_push(list, string);
    it = neo_list_node_next(it);
  }
  len++;
  key = neo_js_context_create_cstring(ctx, "name");
  neo_js_variable_t name = neo_js_variable_get_field(self, ctx, key);
  const uint16_t *errname = ((neo_js_string_t)name->value)->value;
  len += neo_string16_length(errname) + 2;
  const uint16_t *msg = ((neo_js_string_t)message->value)->value;
  len += neo_string16_length(msg) + 1;
  uint16_t string[len];
  uint16_t *dst = string;
  const uint16_t *src = errname;
  while (*src) {
    *dst++ = *src++;
  }
  *dst++ = ':';
  *dst++ = ' ';
  src = msg;
  while (*src) {
    *dst++ = *src++;
  }
  *dst++ = '\n';
  it = neo_list_get_first(list);
  while (it != neo_list_get_tail(list)) {
    const uint16_t *src = neo_list_node_get(it);
    const char *prefix = "    at ";
    while (*prefix) {
      *dst++ = *prefix++;
    }
    while (*src) {
      *dst++ = *src++;
    }
    *dst++ = '\n';
    it = neo_list_node_next(it);
  }
  *dst = 0;
  neo_allocator_free(allocator, list);
  neo_js_variable_t stack = neo_js_context_create_string(ctx, string);
  key = neo_js_context_create_cstring(ctx, "stack");
  neo_js_variable_def_field(self, ctx, key, stack, true, false, true);
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
  constant->error_prototype = neo_js_variable_get_field(
      constant->error_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->error_prototype, "toString",
                    neo_js_error_to_string);
  neo_js_variable_t string = neo_js_context_create_cstring(ctx, "Error");
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "name");
  neo_js_variable_def_field(constant->error_prototype, ctx, key, string, true,
                            false, true);
}