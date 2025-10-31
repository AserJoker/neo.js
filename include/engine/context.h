#ifndef _H_NEO_ENGINE_CONTEXT_
#define _H_NEO_ENGINE_CONTEXT_
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_context_t *neo_js_context_t;
neo_js_context_t neo_create_js_context(neo_js_runtime_t runtime);
void neo_js_context_push_scope(neo_js_context_t self);
void neo_js_context_pop_scope(neo_js_context_t self);
neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self);
neo_js_variable_t neo_js_context_create_variable(neo_js_context_t self,
                                                 neo_js_value_t value);
neo_js_variable_t neo_js_context_create_exception(neo_js_context_t self,
                                                  neo_js_variable_t error);
neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t self);
neo_js_variable_t neo_js_context_create_null(neo_js_context_t self);
neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double value);
neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t self,
                                                bool value);
neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               const uint16_t *value);
neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t self,
                                               uint16_t *description);
neo_js_variable_t neo_js_context_create_object(neo_js_context_t self,
                                               neo_js_variable_t prototype);
neo_js_scope_t neo_js_context_get_scope(neo_js_context_t self);

neo_js_variable_t neo_js_context_format(neo_js_context_t self, const char *fmt,
                                        ...);

neo_js_variable_t neo_js_context_recycle(neo_js_context_t self,
                                         neo_js_variable_t variable);
#ifdef __cplusplus
}
#endif
#endif