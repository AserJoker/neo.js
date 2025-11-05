#include "engine/context.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/boolean.h"
#include "engine/cfunction.h"
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
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t root_scope;
  neo_js_scope_t current_scope;
  neo_list_t callstack;
  neo_js_constant_t constant;
  neo_js_context_type_t type;
};

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t self) {
  self->root_scope = NULL;
  while (self->current_scope) {
    neo_js_context_pop_scope(self);
  }
  neo_allocator_free(allocator, self->callstack);
}

neo_js_context_t neo_create_js_context(neo_js_runtime_t runtime) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root_scope = neo_create_js_scope(allocator, NULL);
  ctx->current_scope = ctx->root_scope;
  neo_list_initialize_t initialize = {true};
  ctx->callstack = neo_create_list(allocator, &initialize);
  neo_js_stackframe_t frame =
      neo_create_js_stackframe(allocator, NULL, NULL, 0, 0);
  ctx->type = NEO_JS_CONTEXT_MODULE;
  neo_list_push(ctx->callstack, frame);
  memset(&ctx->constant, 0, sizeof(neo_js_constant_t));
  neo_js_context_push_scope(ctx);
  neo_initialize_js_constant(ctx);
  neo_js_context_pop_scope(ctx);
  return ctx;
}

neo_js_constant_t *neo_js_context_get_constant(neo_js_context_t self) {
  return &self->constant;
}

neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self) {
  return self->runtime;
}
neo_js_context_type_t neo_js_context_get_type(neo_js_context_t self) {
  return self->type;
}
neo_js_context_type_t neo_js_context_set_type(neo_js_context_t self,
                                              neo_js_context_type_t type) {
  neo_js_context_type_t current = self->type;
  self->type = type;
  return current;
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

neo_js_scope_t neo_js_context_get_scope(neo_js_context_t self) {
  return self->current_scope;
}
neo_js_scope_t neo_js_context_get_root_scope(neo_js_context_t self) {
  return self->root_scope;
}
neo_js_scope_t neo_js_context_set_scope(neo_js_context_t self,
                                        neo_js_scope_t scope) {
  neo_js_scope_t current = self->current_scope;
  self->current_scope = scope;
  return current;
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

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t self,
                                                 neo_js_value_t value) {
  return neo_js_scope_create_variable(self->current_scope, value, NULL);
}

neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_undefined_t undefined = neo_create_js_undefined(allocator);
  neo_js_value_t value = neo_js_undefined_to_value(undefined);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_null(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_null_t null = neo_create_js_null(allocator);
  neo_js_value_t value = neo_js_null_to_value(null);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double val) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_number_t number = neo_create_js_number(allocator, val);
  neo_js_value_t value = neo_js_number_to_value(number);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t self,
                                                bool val) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_boolean_t boolean = neo_create_js_boolean(allocator, val);
  neo_js_value_t value = neo_js_boolean_to_value(boolean);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               const uint16_t *val) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_string_t string = neo_create_js_string(allocator, val);
  neo_js_value_t value = neo_js_string_to_value(string);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_cstring(neo_js_context_t self,
                                                const char *val) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  uint16_t *str = neo_string_to_string16(allocator, val);
  neo_js_variable_t string = neo_js_context_create_string(self, str);
  neo_allocator_free(allocator, str);
  return string;
}
neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t self,
                                               const uint16_t *description) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_symbol_t symbol = neo_create_js_symbol(allocator, description);
  neo_js_value_t value = neo_js_symbol_to_value(symbol);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_exception(neo_js_context_t self,
                                                  neo_js_variable_t error) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_exception_t exception =
      neo_create_js_exception(allocator, error->value);
  neo_js_value_t value = neo_js_exception_to_value(exception);
  neo_js_variable_t result = neo_js_context_create_variable(self, value);
  return result;
}

neo_js_variable_t neo_js_context_create_object(neo_js_context_t self,
                                               neo_js_variable_t prototype) {
  if (!prototype) {
    prototype = self->constant.object_prototype;
  }
  if (!prototype) {
    prototype = neo_js_context_create_null(self);
  }
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_object_t object = neo_create_js_object(allocator, prototype->value);
  neo_js_value_t value = neo_js_object_to_value(object);
  return neo_js_context_create_variable(self, value);
}
neo_js_variable_t neo_js_context_create_cfunction(neo_js_context_t self,
                                                  neo_js_cfunc_t callee,
                                                  const char *name) {
  neo_js_variable_t prototype = self->constant.function_prototype;
  if (!prototype) {
    prototype = neo_js_context_create_null(self);
  }
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_cfunction_t cfunction =
      neo_create_js_cfunction(allocator, callee, prototype->value);
  neo_js_value_t value = neo_js_cfunction_to_value(cfunction);
  neo_js_variable_t result = neo_js_context_create_variable(self, value);
  prototype = neo_js_context_create_object(self, NULL);
  neo_js_variable_t key = neo_js_context_create_cstring(self, "prototype");
  neo_js_variable_def_field(result, self, key, prototype, false, false, false);
  key = neo_js_context_create_cstring(self, "key");
  neo_js_variable_t funcname = neo_js_context_create_cstring(self, name);
  neo_js_variable_def_field(result, self, key, funcname, false, false, false);
  return result;
}

neo_js_variable_t neo_js_context_construct(neo_js_context_t self,
                                           neo_js_variable_t constructor,
                                           size_t argc,
                                           neo_js_variable_t *argv) {
  if (constructor->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(self, "%v is not a constructor", constructor);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(self, error);
  }
  neo_js_variable_t prototype =
      neo_js_context_create_cstring(self, "prototype");
  prototype = neo_js_variable_get_field(constructor, self, prototype);
  if (prototype->value->type == NEO_JS_TYPE_EXCEPTION) {
    return prototype;
  }
  neo_js_variable_t object = neo_js_context_create_object(self, prototype);
  neo_js_variable_t key = neo_js_context_create_cstring(self, "constructor");
  neo_js_variable_def_field(object, self, key, constructor, true, false, true);
  neo_js_variable_t res =
      neo_js_variable_call(constructor, self, object, argc, argv);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    return res;
  }
  if (res->value->type >= NEO_JS_TYPE_OBJECT) {
    return res;
  }
  return object;
}

neo_js_variable_t neo_js_context_load(neo_js_context_t self,
                                      const uint16_t *name) {
  neo_js_variable_t variable = NULL;
  neo_js_scope_t scope = self->current_scope;
  while (scope) {
    variable = neo_js_scope_get_variable(self->current_scope, name);
    if (variable) {
      break;
    }
    scope = neo_js_scope_get_parent(scope);
  }
  return variable;
}

void neo_js_context_recycle(neo_js_context_t self, neo_js_value_t value) {
  neo_js_scope_t scope = self->current_scope;
}

neo_js_variable_t neo_js_context_format(neo_js_context_t self, const char *fmt,
                                        ...) {
  size_t len = 0;
  va_list args;
  va_start(args, fmt);
  const char *psrc = fmt;
  while (*psrc) {
    neo_utf8_char chr = neo_utf8_read_char(psrc);
    if (*psrc == '%') {
      len++;
      psrc++;
      if (*psrc == 0) {
        break;
      } else if (*psrc == 'v') {
        psrc++;
        neo_js_variable_t arg = va_arg(args, neo_js_variable_t);
        if (arg->value->type == NEO_JS_TYPE_SYMBOL) {
          neo_js_symbol_t symbol = (neo_js_symbol_t)arg->value;
          len += neo_string16_length(symbol->description) + 8;
        } else {
          neo_js_string_t string = (neo_js_string_t)arg->value;
          len += neo_string16_length(string->value);
        }
        continue;
      }
    }
    psrc = chr.end;
    uint32_t utf32 = neo_utf8_char_to_utf32(chr);
    len += neo_utf32_to_utf16(utf32, NULL);
  }
  va_end(args);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  uint16_t *string =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);

  va_start(args, fmt);
  psrc = fmt;
  uint16_t *dst = string;
  while (*psrc) {
    neo_utf8_char chr = neo_utf8_read_char(psrc);
    if (*psrc == '%') {
      *dst++ = *psrc++;
      if (*psrc == 0) {
        break;
      } else if (*psrc == 'v') {
        psrc++;
        neo_js_variable_t arg = va_arg(args, neo_js_variable_t);
        if (arg->value->type == NEO_JS_TYPE_SYMBOL) {
          neo_js_symbol_t symbol = (neo_js_symbol_t)arg->value;
          const char *prefix = "Symbol(";
          const char *stuffix = ")";
          while (*prefix) {
            *dst++ = *prefix++;
          }
          const uint16_t *src = symbol->description;
          while (*src) {
            *dst++ = *src++;
          }
          while (*stuffix) {
            *dst++ = *stuffix++;
          }
        } else {
          neo_js_string_t string = (neo_js_string_t)arg->value;
          const uint16_t *src = string->value;
          while (*src) {
            *dst++ = *src++;
          }
        }
        continue;
      }
    }
    psrc = chr.end;
    uint32_t utf32 = neo_utf8_char_to_utf32(chr);
    dst += neo_utf32_to_utf16(utf32, dst);
  }
  va_end(args);
  neo_js_variable_t result = neo_js_context_create_string(self, string);
  neo_allocator_free(allocator, string);
  return result;
}
neo_js_variable_t neo_js_context_get_argument(neo_js_context_t self,
                                              size_t argc,
                                              neo_js_variable_t *argv,
                                              size_t idx) {
  if (idx >= argc) {
    return neo_js_context_create_undefined(self);
  }
  return argv[idx];
}