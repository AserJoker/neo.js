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

neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t ctx);

neo_js_scope_t neo_js_context_get_scope(neo_js_context_t ctx);

neo_js_scope_t neo_js_context_get_root(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_global(neo_js_context_t ctx);

neo_js_scope_t neo_js_context_push_scope(neo_js_context_t ctx);

neo_js_scope_t neo_js_context_pop_scope(neo_js_context_t ctx);

neo_list_t neo_js_context_get_stacktrace(neo_js_context_t ctx, uint32_t line,
                                         uint32_t column);

void neo_js_context_push_stackframe(neo_js_context_t ctx,
                                    const wchar_t *filename,
                                    const wchar_t *function, uint32_t column,
                                    uint32_t line);

void neo_js_context_pop_stackframe(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_object_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_function_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_string_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_boolean_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_number_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_symbol_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_array_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_clone_variable(neo_js_context_t ctx,
                                                neo_js_variable_t variable);

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t ctx,
                                                 neo_js_handle_t handle);

neo_js_variable_t neo_js_context_to_primitive(neo_js_context_t ctx,
                                              neo_js_variable_t variable);

neo_js_variable_t neo_js_context_to_object(neo_js_context_t ctx,
                                           neo_js_variable_t variable);

neo_js_variable_t neo_js_context_get_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field);

neo_js_variable_t neo_js_context_set_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field,
                                           neo_js_variable_t value);

neo_js_variable_t neo_js_context_del_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field);

neo_js_variable_t neo_js_context_call(neo_js_context_t ctx,
                                      neo_js_variable_t callee,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_context_construct(neo_js_context_t ctx,
                                           neo_js_variable_t constructor,
                                           uint32_t argc,
                                           neo_js_variable_t *argv);

neo_js_variable_t neo_js_context_create_error(neo_js_context_t ctx,
                                              const wchar_t *type,
                                              const wchar_t *message);

neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_create_number(neo_js_context_t ctx,
                                               double value);

neo_js_variable_t neo_js_context_create_string(neo_js_context_t ctx,
                                               const wchar_t *value);

neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t ctx,
                                                bool value);

neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t ctx,
                                               wchar_t *description);

neo_js_variable_t neo_js_context_create_object(neo_js_context_t ctx,
                                               neo_js_variable_t prototype);

neo_js_variable_t
neo_js_context_create_cfunction(neo_js_context_t ctx, const wchar_t *name,
                                neo_js_cfunction_fn_t function);

const wchar_t *neo_js_context_typeof(neo_js_context_t ctx,
                                     neo_js_variable_t variable);

neo_js_variable_t neo_js_context_to_string(neo_js_context_t ctx,
                                           neo_js_variable_t variable);

neo_js_variable_t neo_js_context_to_boolean(neo_js_context_t ctx,
                                            neo_js_variable_t variable);

neo_js_variable_t neo_js_context_to_number(neo_js_context_t ctx,
                                           neo_js_variable_t variable);

bool neo_js_context_is_equal(neo_js_context_t ctx, neo_js_variable_t variable,
                             neo_js_variable_t another);

bool neo_js_context_is_not_equal(neo_js_context_t ctx,
                                 neo_js_variable_t variable,
                                 neo_js_variable_t another);

#ifdef __cplusplus
}
#endif
#endif