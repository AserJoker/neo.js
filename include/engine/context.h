#ifndef _H_NEO_ENGINE_CONTEXT_
#define _H_NEO_ENGINE_CONTEXT_
#include "compiler/program.h"
#include "core/bigint.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_vm_t *neo_js_vm_t;

typedef enum _neo_js_context_type_t {
  NEO_JS_CONTEXT_MODULE,
  NEO_JS_CONTEXT_ASYNC_FUNCTION,
  NEO_JS_CONTEXT_GENERATOR_FUNCTION,
  NEO_JS_CONTEXT_ASYNC_GENERATOR_FUNCTION,
  NEO_JS_CONTEXT_FUNCTION,
  NEO_JS_CONTEXT_CONSTRUCT,
} neo_js_context_type_t;
typedef struct _neo_js_context_t *neo_js_context_t;
typedef void (*neo_js_error_callback)(neo_js_context_t ctx,
                                      neo_js_variable_t error);
neo_js_context_t neo_create_js_context(neo_js_runtime_t runtime);
void neo_js_context_set_error_callback(neo_js_context_t self,
                                       neo_js_error_callback callback);
neo_js_error_callback neo_js_context_get_error_callback(neo_js_context_t self);
neo_js_constant_t neo_js_context_get_constant(neo_js_context_t self);
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
void neo_js_context_push_callstack(neo_js_context_t self, const char *filename,
                                   const uint16_t *funcname, uint32_t line,
                                   uint32_t column);
void neo_js_context_pop_callstack(neo_js_context_t self);
neo_list_t neo_js_context_get_callstack(neo_js_context_t self);
neo_list_t neo_js_context_set_callstack(neo_js_context_t self,
                                        neo_list_t callstack);
neo_list_t neo_js_context_trace(neo_js_context_t self, const char *filename,
                                uint32_t line, uint32_t column);
neo_js_variable_t neo_js_context_create_variable(neo_js_context_t self,
                                                 neo_js_value_t value);
neo_js_variable_t neo_js_context_get_uninitialized(neo_js_context_t self);
neo_js_variable_t neo_js_context_get_undefined(neo_js_context_t self);
neo_js_variable_t neo_js_context_get_null(neo_js_context_t self);
neo_js_variable_t neo_js_context_get_nan(neo_js_context_t self);
neo_js_variable_t neo_js_context_get_infinity(neo_js_context_t self);
neo_js_variable_t neo_js_context_get_true(neo_js_context_t self);
neo_js_variable_t neo_js_context_get_false(neo_js_context_t self);

neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double val);
neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               const uint16_t *val);
neo_js_variable_t neo_js_context_create_cstring(neo_js_context_t self,
                                                const char *val);
neo_js_variable_t neo_js_context_create_cbigint(neo_js_context_t self,
                                                const char *val);
neo_js_variable_t neo_js_context_create_bigint(neo_js_context_t self,
                                               neo_bigint_t val);
neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t self,
                                               const uint16_t *description);
neo_js_variable_t neo_js_context_create_exception(neo_js_context_t self,
                                                  neo_js_variable_t error);
neo_js_variable_t neo_js_context_create_object(neo_js_context_t self,
                                               neo_js_variable_t prototype);
neo_js_variable_t neo_js_context_create_array(neo_js_context_t ctx);
neo_js_variable_t neo_js_context_create_cfunction(neo_js_context_t self,
                                                  neo_js_cfunc_t callee,
                                                  const char *name);
neo_js_variable_t neo_js_context_create_function(neo_js_context_t self,
                                                 neo_program_t program);
neo_js_variable_t neo_js_context_create_generator(neo_js_context_t self,
                                                  neo_program_t program);
neo_js_variable_t neo_js_context_create_signal(neo_js_context_t self,
                                               uint32_t type, const void *msg);
neo_js_variable_t neo_js_context_create_interrupt(neo_js_context_t self,
                                                  neo_js_variable_t value,
                                                  size_t address,
                                                  neo_program_t program,
                                                  neo_js_vm_t vm);
neo_js_variable_t neo_js_context_load(neo_js_context_t self, const char *name);
neo_js_variable_t neo_js_context_store(neo_js_context_t self, const char *name,
                                       neo_js_variable_t variable);
neo_js_variable_t neo_js_context_def(neo_js_context_t self, const char *name,
                                     neo_js_variable_t variable);
neo_js_variable_t neo_js_context_get_global(neo_js_context_t self);
neo_js_variable_t neo_js_context_format(neo_js_context_t self, const char *fmt,
                                        ...);
neo_js_variable_t neo_js_context_get_argument(neo_js_context_t self,
                                              size_t argc,
                                              neo_js_variable_t *argv,
                                              size_t idx);
int64_t neo_js_context_create_macro_task(neo_js_context_t self,
                                         neo_js_variable_t callee,
                                         int64_t timeout, bool keep);
void neo_js_context_remove_macro_task(neo_js_context_t self, int64_t idx);
bool neo_js_context_next_task(neo_js_context_t self);
bool neo_js_context_has_task(neo_js_context_t self);
neo_js_variable_t neo_js_context_eval(neo_js_context_t self, const char *source,
                                      const char *filename);

#define NEO_JS_DEF_FIELD(ctx, self, name, value)                               \
  do {                                                                         \
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, name);          \
    neo_js_variable_def_field(self, ctx, key, value, true, false, true);       \
  } while (0)

#define NEO_JS_DEF_METHOD(ctx, self, name, callee)                             \
  do {                                                                         \
    neo_js_variable_t method =                                                 \
        neo_js_context_create_cfunction(ctx, callee, name);                    \
    NEO_JS_DEF_FIELD(ctx, self, name, method);                                 \
  } while (0)

#define NEO_DEF_SYMBOL_METHOD(ctx, self, symbol, name, callee)                 \
  do {                                                                         \
    neo_js_variable_t method =                                                 \
        neo_js_context_create_cfunction(ctx, callee, name);                    \
    neo_js_variable_def_field(self, ctx, symbol, method, true, false, true);   \
  } while (0)
#ifdef __cplusplus
}
#endif
#endif