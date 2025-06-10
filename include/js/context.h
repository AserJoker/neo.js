#ifndef _H_NEO_JS_CONTEXT_
#define _H_NEO_JS_CONTEXT_
#include "core/allocator.h"
#include "js/runtime.h"
#include "js/scope.h"
#include "js/type.h"
#include "js/variable.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_context_t *neo_js_context_t;
neo_js_context_t neo_create_js_context(neo_allocator_t allocator,
                                       neo_js_runtime_t runtime);

neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self);

neo_js_scope_t neo_js_context_get_scope(neo_js_context_t self);

neo_js_scope_t neo_js_context_get_root(neo_js_context_t self);

neo_js_variable_t neo_js_context_get_global(neo_js_context_t self);

neo_js_scope_t neo_js_context_push_scope(neo_js_context_t self);

neo_js_scope_t neo_js_context_pop_scope(neo_js_context_t self);

neo_js_variable_t neo_js_context_clone_variable(neo_js_context_t self,
                                                neo_js_variable_t variable);

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t self,
                                                 neo_js_handle_t handle);

neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t self);

neo_js_variable_t neo_js_context_create_nan(neo_js_context_t self);

neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double value);

neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               wchar_t *value);

neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t self,
                                                bool value);

const wchar_t *neo_js_context_typeof(neo_js_context_t self,
                                     neo_js_variable_t variable);

neo_js_variable_t neo_js_context_tostring(neo_js_context_t self,
                                          neo_js_variable_t variable);

neo_js_variable_t neo_js_context_toboolean(neo_js_context_t self,
                                           neo_js_variable_t variable);

#ifdef __cplusplus
}
#endif
#endif