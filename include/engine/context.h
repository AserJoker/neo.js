#ifndef _H_NEO_ENGINE_CONTEXT_
#define _H_NEO_ENGINE_CONTEXT_
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum _neo_js_context_type_t {
  NEO_JS_CONTEXT_MODULE,
  NEO_JS_CONTEXT_ASYNC_FUNCTION,
  NEO_JS_CONTEXT_GENERATOR_FUNCTION,
  NEO_JS_CONTEXT_ASYNC_GENERATOR_FUNCTION,
  NEO_JS_CONTEXT_FUNCTION,
  NEO_JS_CONTEXT_CONSTRUCT,
} neo_js_context_type_t;
typedef struct _neo_js_context_t *neo_js_context_t;
neo_js_context_t neo_create_js_context(neo_js_runtime_t runtime);
neo_js_constant_t *neo_js_context_get_constant(neo_js_context_t self);
neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self);
neo_js_context_type_t neo_js_context_get_type(neo_js_context_t self);
neo_js_context_type_t neo_js_context_set_type(neo_js_context_t self,
                                              neo_js_context_type_t type);
void neo_js_context_push_scope(neo_js_context_t self);
void neo_js_context_pop_scope(neo_js_context_t self);
neo_js_scope_t neo_js_context_get_scope(neo_js_context_t self);
neo_js_scope_t neo_js_context_get_root_scope(neo_js_context_t self);
neo_js_scope_t neo_js_context_set_scope(neo_js_context_t self,
                                        neo_js_scope_t scope);
void neo_js_context_push_callstack(neo_js_context_t self,
                                   const uint16_t *filename,
                                   const uint16_t *funcname, uint32_t line,
                                   uint32_t column);
void neo_js_context_pop_callstack(neo_js_context_t self);
neo_list_t neo_js_context_get_callstack(neo_js_context_t self);
neo_list_t neo_js_context_set_callstack(neo_js_context_t self,
                                        neo_list_t callstack);
neo_list_t neo_js_context_trace(neo_js_context_t self, uint32_t line,
                                uint32_t column);
neo_js_variable_t neo_js_context_create_variable(neo_js_context_t self,
                                                 neo_js_value_t value);
neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t self);
neo_js_variable_t neo_js_context_create_null(neo_js_context_t self);
neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double val);
neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t self,
                                                bool val);
neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               const uint16_t *val);
neo_js_variable_t neo_js_context_create_cstring(neo_js_context_t self,
                                                const char *val);
neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t self,
                                               const uint16_t *description);
neo_js_variable_t neo_js_context_create_exception(neo_js_context_t self,
                                                  neo_js_variable_t error);
neo_js_variable_t neo_js_context_create_object(neo_js_context_t self,
                                               neo_js_variable_t prototype);
neo_js_variable_t neo_js_context_create_cfunction(neo_js_context_t self,
                                                  neo_js_cfunc_t callee,
                                                  const char *name);
neo_js_variable_t neo_js_context_load(neo_js_context_t self,
                                      const uint16_t *name);
neo_js_variable_t neo_js_context_format(neo_js_context_t self, const char *fmt,
                                        ...);
neo_js_variable_t neo_js_context_get_argument(neo_js_context_t self,
                                              size_t argc,
                                              neo_js_variable_t *argv,
                                              size_t idx);

#define NEO_JS_DEF_METHOD(ctx, self, name, callee)                             \
  do {                                                                         \
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, #name);         \
    neo_js_variable_t method =                                                 \
        neo_js_context_create_cfunction(ctx, callee, #name);                   \
    neo_js_variable_def_field(self, ctx, key, method, true, false, true);      \
  } while (0)

#define NEO_DEF_SYMBOL_METHOD(ctx, self, symbol, name, callee)                 \
  do {                                                                         \
    neo_js_variable_t method =                                                 \
        neo_js_context_create_cfunction(ctx, callee, #name);                   \
    neo_js_variable_def_field(self, ctx, symbol, method, true, false, true);   \
  } while (0)
#ifdef __cplusplus
}
#endif
#endif