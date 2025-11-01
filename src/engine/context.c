#include "engine/context.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/string.h"
#include "engine/boolean.h"
#include "engine/exception.h"
#include "engine/null.h"
#include "engine/number.h"
#include "engine/object.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/stackframe.h"
#include "engine/string.h"
#include "engine/symbol.h"
#include "engine/undefined.h"
#include "engine/variable.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t root_scope;
  neo_js_scope_t current_scope;
  neo_list_t callstack;
};

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t self) {
  self->root_scope = NULL;
  while (self->current_scope) {
    neo_js_context_pop_scope(self);
  }
}

neo_js_context_t neo_create_js_context(neo_js_runtime_t runtime) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root_scope = neo_create_js_scope(allocator, NULL);
  ctx->current_scope = ctx->root_scope;
  return ctx;
}

void neo_js_context_push_scope(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  self->current_scope = neo_create_js_scope(allocator, self->current_scope);
}

void neo_js_context_pop_scope(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_scope_t scope = self->current_scope;
  self->current_scope = neo_js_scope_get_parent(scope);
  neo_allocator_free(allocator, scope);
}
neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self) {
  return self->runtime;
}
neo_js_variable_t neo_js_context_create_variable(neo_js_context_t self,
                                                 neo_js_value_t value) {
  return neo_js_scope_create_variable(self->current_scope, value, NULL);
}
neo_js_variable_t neo_js_context_create_exception(neo_js_context_t self,
                                                  neo_js_variable_t error) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_exception_t exception = neo_create_js_exception(allocator, error);
  neo_js_value_t val = neo_js_exception_to_value(exception);
  neo_js_variable_t result =
      neo_js_scope_create_variable(self->current_scope, val, NULL);
  neo_js_variable_add_parent(error, result);
  return result;
}
neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_undefined_t undefined = neo_create_js_undefined(allocator);
  neo_js_value_t val = neo_js_undefined_to_value(undefined);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_null(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_null_t null = neo_create_js_null(allocator);
  neo_js_value_t val = neo_js_null_to_value(null);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_number_t number = neo_create_js_number(allocator, value);
  neo_js_value_t val = neo_js_number_to_value(number);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t self,
                                                bool value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_boolean_t boolean = neo_create_js_boolean(allocator, value);
  neo_js_value_t val = neo_js_boolean_to_value(boolean);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               const uint16_t *value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_string_t string = neo_create_js_string(allocator, value);
  neo_js_value_t val = neo_js_string_to_value(string);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t self,
                                               uint16_t *description) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_symbol_t symbol = neo_create_js_symbol(allocator, description);
  neo_js_value_t val = neo_js_symbol_to_value(symbol);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_object(neo_js_context_t self,
                                               neo_js_variable_t prototype) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_object_t object = neo_create_js_object(allocator, prototype);
  neo_js_value_t val = neo_js_object_to_value(object);
  neo_js_variable_t result =
      neo_js_scope_create_variable(self->current_scope, val, NULL);
  neo_js_variable_add_parent(prototype, result);
  return result;
}

neo_js_scope_t neo_js_context_get_scope(neo_js_context_t self) {
  return self->current_scope;
}
neo_js_scope_t neo_js_context_get_root_scope(neo_js_context_t self) {
  return self->root_scope;
}

void neo_js_context_push_callstack(neo_js_context_t self,
                                   const uint16_t *filename,
                                   const uint16_t *funcname, uint32_t line,
                                   uint32_t column) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_list_node_t last = neo_list_get_last(self->callstack);
  neo_js_stackframe_t frame = neo_list_node_get(last);
  frame->column = column;
  frame->line = line;
  frame = neo_create_js_stackframe(allocator, filename, funcname, 0, 0);
  neo_list_push(self->callstack, frame);
}
void neo_js_context_pop_callstack(neo_js_context_t self) {
  neo_list_pop(self->callstack);
}
neo_list_t neo_js_context_get_callstack(neo_js_context_t self) {
  return self->callstack;
}
neo_list_t neo_js_context_set_callstack(neo_js_context_t self,
                                        neo_list_t callstack) {
  neo_list_t current = self->callstack;
  self->callstack = callstack;
  return current;
}
neo_list_t neo_js_context_trace(neo_js_context_t self, uint32_t line,
                                uint32_t column) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_list_initialize_t initialize = {true};
  neo_list_t callstack = neo_create_list(allocator, &initialize);
  neo_list_node_t it = neo_list_get_first(self->callstack);
  while (it != neo_list_get_tail(self->callstack)) {
    neo_js_stackframe_t frame = neo_list_node_get(it);
    frame =
        neo_create_js_stackframe(allocator, frame->filename, frame->funcname,
                                 frame->line, frame->column);
    neo_list_push(callstack, frame);
    it = neo_list_node_next(it);
  }
  it = neo_list_get_tail(callstack);
  neo_js_stackframe_t frame = neo_list_node_get(it);
  frame->column = column;
  frame->line = line;
  return callstack;
}

neo_js_variable_t neo_js_context_format(neo_js_context_t self, const char *fmt,
                                        ...) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  uint16_t *format = neo_string_to_string16(allocator, fmt);
  const uint16_t *pformat = format;
  size_t len = 0;
  va_list args;
  va_start(args, fmt);
  while (*pformat) {
    if (*pformat == '%') {
      pformat++;
      switch (*(pformat)) {
      case 'v': {
        neo_js_variable_t arg = va_arg(args, neo_js_variable_t);
        if (arg->value->type != NEO_JS_TYPE_SYMBOL) {
          arg = neo_js_variable_to_string(arg, self);
          const uint16_t *str = ((neo_js_string_t)(arg->value))->value;
          len += neo_string16_length(str);
        } else {
          arg = neo_js_variable_to_string(arg, self);
          const uint16_t *str = ((neo_js_symbol_t)(arg->value))->description;
          len += neo_string16_length(str) + 2;
        }
        break;
      }
      default:
        len += 2;
        break;
      }
      pformat++;
    } else {
      pformat++;
      len++;
    }
  }
  va_end(args);
  uint16_t *result =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
  uint16_t *dst = result;
  va_start(args, fmt);
  while (*pformat) {
    if (*pformat == '%') {
      pformat++;
      switch (*(pformat)) {
      case 's': {
        neo_js_variable_t arg = va_arg(args, neo_js_variable_t);
        if (arg->value->type != NEO_JS_TYPE_SYMBOL) {
          arg = neo_js_variable_to_string(arg, self);
          const uint16_t *str = ((neo_js_string_t)(arg->value))->value;
          memcpy(dst, str, sizeof(uint16_t) * neo_string16_length(str));
        } else {
          *dst++ = '[';
          const uint16_t *str = ((neo_js_symbol_t)(arg->value))->description;
          memcpy(dst, str, sizeof(uint16_t) * neo_string16_length(str));
          *dst++ = ']';
        }
        break;
      }
      default:
        *dst++ = '%';
        *dst++ = *pformat;
        break;
      }
      pformat++;
    } else {
      *dst++ = *pformat++;
    }
  }
  va_end(args);
  neo_allocator_free(allocator, format);
  neo_js_variable_t message = neo_js_context_create_string(self, result);
  neo_allocator_free(allocator, message);
  return message;
}

neo_js_variable_t neo_js_context_recycle(neo_js_context_t self,
                                         neo_js_variable_t variable) {
  neo_js_scope_set_variable(self->current_scope, variable, NULL);
  return variable;
}