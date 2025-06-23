#ifndef _H_NEO_ENGINE_CONTEXT_
#define _H_NEO_ENGINE_CONTEXT_
#include "core/allocator.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_engine_context_t *neo_engine_context_t;

neo_engine_context_t neo_create_js_context(neo_allocator_t allocator,
                                           neo_engine_runtime_t runtime);

neo_engine_runtime_t neo_engine_context_get_runtime(neo_engine_context_t ctx);

static inline neo_allocator_t
neo_engine_context_get_allocator(neo_engine_context_t ctx) {
  return neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
}

neo_engine_scope_t neo_engine_context_get_scope(neo_engine_context_t ctx);

neo_engine_scope_t neo_engine_context_get_root(neo_engine_context_t ctx);

neo_engine_variable_t neo_engine_context_get_global(neo_engine_context_t ctx);

neo_engine_scope_t neo_engine_context_push_scope(neo_engine_context_t ctx);

neo_engine_scope_t neo_engine_context_pop_scope(neo_engine_context_t ctx);

neo_list_t neo_engine_context_get_stacktrace(neo_engine_context_t ctx,
                                             uint32_t line, uint32_t column);

void neo_engine_context_push_stackframe(neo_engine_context_t ctx,
                                        const wchar_t *filename,
                                        const wchar_t *function,
                                        uint32_t column, uint32_t line);

void neo_engine_context_pop_stackframe(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_get_object_constructor(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_get_function_constructor(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_get_string_constructor(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_get_boolean_constructor(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_get_number_constructor(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_get_symbol_constructor(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_get_array_constructor(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_create_variable(neo_engine_context_t ctx,
                                   neo_engine_handle_t handle);

neo_engine_variable_t
neo_engine_context_to_primitive(neo_engine_context_t ctx,
                                neo_engine_variable_t variable);

neo_engine_variable_t
neo_engine_context_to_object(neo_engine_context_t ctx,
                             neo_engine_variable_t variable);

neo_engine_variable_t neo_engine_context_get_field(neo_engine_context_t ctx,
                                                   neo_engine_variable_t object,
                                                   neo_engine_variable_t field);

neo_engine_variable_t neo_engine_context_set_field(neo_engine_context_t ctx,
                                                   neo_engine_variable_t object,
                                                   neo_engine_variable_t field,
                                                   neo_engine_variable_t value);

neo_engine_variable_t neo_engine_context_has_field(neo_engine_context_t ctx,
                                                   neo_engine_variable_t object,
                                                   neo_engine_variable_t field);

neo_engine_variable_t neo_engine_context_def_field(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field, neo_engine_variable_t value, bool configurable,
    bool enumable, bool writable);

neo_engine_variable_t neo_engine_context_def_accessor(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field, neo_engine_variable_t getter,
    neo_engine_variable_t setter, bool configurable, bool enumable);

neo_engine_variable_t
neo_engine_context_get_internal(neo_engine_context_t ctx,
                                neo_engine_variable_t object,
                                const wchar_t *field);

neo_engine_variable_t neo_engine_context_set_internal(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    const wchar_t *field, neo_engine_variable_t value);

neo_engine_variable_t neo_engine_context_del_field(neo_engine_context_t ctx,
                                                   neo_engine_variable_t object,
                                                   neo_engine_variable_t field);

neo_engine_variable_t neo_engine_context_clone(neo_engine_context_t ctx,
                                               neo_engine_variable_t self);

neo_engine_variable_t
neo_engine_context_assigment(neo_engine_context_t ctx,
                             neo_engine_variable_t self,
                             neo_engine_variable_t target);

neo_engine_variable_t neo_engine_context_call(neo_engine_context_t ctx,
                                              neo_engine_variable_t callee,
                                              neo_engine_variable_t self,
                                              uint32_t argc,
                                              neo_engine_variable_t *argv);

neo_engine_variable_t
neo_engine_context_construct(neo_engine_context_t ctx,
                             neo_engine_variable_t constructor, uint32_t argc,
                             neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_context_create_error(neo_engine_context_t ctx,
                                                      const wchar_t *type,
                                                      const wchar_t *message);

neo_engine_variable_t
neo_engine_context_create_undefined(neo_engine_context_t ctx);

neo_engine_variable_t neo_engine_context_create_null(neo_engine_context_t ctx);

neo_engine_variable_t neo_engine_context_create_number(neo_engine_context_t ctx,
                                                       double value);

neo_engine_variable_t neo_engine_context_create_string(neo_engine_context_t ctx,
                                                       const wchar_t *value);

neo_engine_variable_t
neo_engine_context_create_boolean(neo_engine_context_t ctx, bool value);

neo_engine_variable_t
neo_engine_context_create_symbol(neo_engine_context_t ctx,
                                 const wchar_t *description);

neo_engine_variable_t
neo_engine_context_create_object(neo_engine_context_t ctx,
                                 neo_engine_variable_t prototype,
                                 neo_engine_object_t object);

neo_engine_variable_t neo_engine_context_create_array(neo_engine_context_t ctx);

neo_engine_variable_t
neo_engine_context_create_cfunction(neo_engine_context_t ctx,
                                    const wchar_t *name,
                                    neo_engine_cfunction_fn_t function);

const wchar_t *neo_engine_context_typeof(neo_engine_context_t ctx,
                                         neo_engine_variable_t variable);

neo_engine_variable_t
neo_engine_context_to_string(neo_engine_context_t ctx,
                             neo_engine_variable_t variable);

neo_engine_variable_t
neo_engine_context_to_boolean(neo_engine_context_t ctx,
                              neo_engine_variable_t variable);

neo_engine_variable_t
neo_engine_context_to_number(neo_engine_context_t ctx,
                             neo_engine_variable_t variable);

bool neo_engine_context_is_equal(neo_engine_context_t ctx,
                                 neo_engine_variable_t variable,
                                 neo_engine_variable_t another);

bool neo_engine_context_is_not_equal(neo_engine_context_t ctx,
                                     neo_engine_variable_t variable,
                                     neo_engine_variable_t another);

bool neo_engine_context_instance_of(neo_engine_context_t ctx,
                                    neo_engine_variable_t variable,
                                    neo_engine_variable_t constructor);

#ifdef __cplusplus
}
#endif
#endif