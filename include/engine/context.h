#ifndef _H_NEO_ENGINE_CONTEXT_
#define _H_NEO_ENGINE_CONTEXT_
#include "compiler/program.h"
#include "core/allocator.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_vm_t *neo_js_vm_t;

typedef struct _neo_js_context_t *neo_js_context_t;

typedef enum _neo_js_error_type_t {
  NEO_ERROR_SYNTAX,
  NEO_ERROR_RANGE,
  NEO_ERROR_TYPE,
  NEO_ERROR_REFERENCE
} neo_js_error_type_t;

neo_js_context_t neo_create_js_context(neo_allocator_t allocator,
                                       neo_js_runtime_t runtime);

neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t ctx);

static inline neo_allocator_t
neo_js_context_get_allocator(neo_js_context_t ctx) {
  return neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
}

void neo_js_context_next_tick(neo_js_context_t ctx);

bool neo_js_context_is_ready(neo_js_context_t ctx);

uint32_t neo_js_context_create_micro_task(neo_js_context_t ctx,
                                          neo_js_variable_t callable,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv,
                                          uint64_t timeout, bool keepalive);

uint32_t neo_js_context_create_macro_task(neo_js_context_t ctx,
                                          neo_js_variable_t callable,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv,
                                          uint64_t timeout, bool keepalive);

void neo_js_context_kill_micro_task(neo_js_context_t ctx, uint32_t id);

void neo_js_context_kill_macro_task(neo_js_context_t ctx, uint32_t id);

neo_js_scope_t neo_js_context_get_scope(neo_js_context_t ctx);

neo_js_scope_t neo_js_context_set_scope(neo_js_context_t ctx,
                                        neo_js_scope_t scope);

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

neo_js_variable_t neo_js_context_get_error_constructor(neo_js_context_t ctx);

neo_js_variable_t
neo_js_context_get_type_error_constructor(neo_js_context_t ctx);

neo_js_variable_t
neo_js_context_get_range_error_constructor(neo_js_context_t ctx);

neo_js_variable_t
neo_js_context_get_reference_error_constructor(neo_js_context_t ctx);

neo_js_variable_t
neo_js_context_get_syntax_error_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_object_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_function_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_string_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_boolean_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_number_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_symbol_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_array_constructor(neo_js_context_t ctx);

neo_js_variable_t
neo_js_context_get_array_iterator_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_get_promise_constructor(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t ctx,
                                                 neo_js_handle_t handle,
                                                 const wchar_t *name);

neo_js_variable_t neo_js_context_def_variable(neo_js_context_t ctx,
                                              neo_js_variable_t variable,
                                              const wchar_t *name);

neo_js_variable_t neo_js_context_store_variable(neo_js_context_t ctx,
                                                neo_js_variable_t variable,
                                                const wchar_t *name);

neo_js_variable_t neo_js_context_load_variable(neo_js_context_t ctx,
                                               const wchar_t *name);

neo_js_variable_t neo_js_context_extends(neo_js_context_t ctx,
                                         neo_js_variable_t variable,
                                         neo_js_variable_t parent);

neo_js_variable_t neo_js_context_to_primitive(neo_js_context_t ctx,
                                              neo_js_variable_t variable,
                                              const wchar_t *hint);

neo_js_variable_t neo_js_context_to_object(neo_js_context_t ctx,
                                           neo_js_variable_t variable);

neo_js_variable_t neo_js_context_get_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field);

neo_js_variable_t neo_js_context_set_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field,
                                           neo_js_variable_t value);

bool neo_js_context_has_field(neo_js_context_t ctx, neo_js_variable_t object,
                              neo_js_variable_t field);

neo_js_variable_t
neo_js_context_def_field(neo_js_context_t ctx, neo_js_variable_t object,
                         neo_js_variable_t field, neo_js_variable_t value,
                         bool configurable, bool enumable, bool writable);

neo_js_variable_t neo_js_context_def_accessor(neo_js_context_t ctx,
                                              neo_js_variable_t object,
                                              neo_js_variable_t field,
                                              neo_js_variable_t getter,
                                              neo_js_variable_t setter,
                                              bool configurable, bool enumable);

neo_js_variable_t neo_js_context_get_internal(neo_js_context_t ctx,
                                              neo_js_variable_t object,
                                              const wchar_t *field);

void neo_js_context_set_internal(neo_js_context_t ctx, neo_js_variable_t object,
                                 const wchar_t *field, neo_js_variable_t value);

void *neo_js_context_get_opaque(neo_js_context_t ctx, neo_js_variable_t object,
                                const wchar_t *field);

void neo_js_context_set_opaque(neo_js_context_t ctx, neo_js_variable_t object,
                               const wchar_t *field, void *value);

bool neo_js_context_is_thenable(neo_js_context_t ctx,
                                neo_js_variable_t variable);

neo_js_variable_t neo_js_context_create_coroutine(neo_js_context_t ctx,
                                                  neo_js_vm_t vm,
                                                  neo_program_t program,
                                                  neo_js_scope_t scope);

void neo_js_context_recycle_coroutine(neo_js_context_t ctx,
                                      neo_js_variable_t coroutine);

neo_js_variable_t neo_js_context_del_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field);

neo_js_variable_t neo_js_context_get_keys(neo_js_context_t ctx,
                                          neo_js_variable_t variable);

neo_js_variable_t neo_js_context_clone(neo_js_context_t ctx,
                                       neo_js_variable_t self);

neo_js_variable_t neo_js_context_assigment(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           neo_js_variable_t target);

neo_js_variable_t neo_js_context_call(neo_js_context_t ctx,
                                      neo_js_variable_t callee,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_context_construct(neo_js_context_t ctx,
                                           neo_js_variable_t constructor,
                                           uint32_t argc,
                                           neo_js_variable_t *argv);

neo_js_variable_t neo_js_context_create_error(neo_js_context_t ctx,
                                              neo_js_error_type_t type,
                                              const wchar_t *message);

neo_js_variable_t neo_js_context_create_value_error(neo_js_context_t ctx,
                                                    neo_js_variable_t value);

neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_create_uninitialize(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_create_null(neo_js_context_t ctx);

neo_js_variable_t neo_js_context_create_number(neo_js_context_t ctx,
                                               double value);

neo_js_variable_t neo_js_context_create_string(neo_js_context_t ctx,
                                               const wchar_t *value);

neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t ctx,
                                                bool value);

neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t ctx,
                                               const wchar_t *description);

neo_js_variable_t neo_js_context_create_object(neo_js_context_t ctx,
                                               neo_js_variable_t prototype,
                                               neo_js_object_t object);

neo_js_variable_t neo_js_context_create_array(neo_js_context_t ctx,
                                              uint32_t length);

neo_js_variable_t neo_js_context_create_interrupt(neo_js_context_t ctx,
                                                  neo_js_variable_t variable,
                                                  size_t offset,
                                                  neo_js_interrupt_type_t type);

neo_js_variable_t
neo_js_context_create_cfunction(neo_js_context_t ctx, const wchar_t *name,
                                neo_js_cfunction_fn_t cfunction);

neo_js_variable_t neo_js_context_create_function(neo_js_context_t ctx,
                                                 neo_program_t program);

neo_js_variable_t
neo_js_context_create_generator_function(neo_js_context_t ctx,
                                         neo_program_t program);

neo_js_variable_t neo_js_context_create_async_function(neo_js_context_t ctx,
                                                       neo_program_t program);

void neo_js_context_bind(neo_js_context_t ctx, neo_js_variable_t func,
                         neo_js_variable_t self);

neo_js_variable_t neo_js_context_typeof(neo_js_context_t ctx,
                                        neo_js_variable_t variable);

neo_js_variable_t neo_js_context_to_string(neo_js_context_t ctx,
                                           neo_js_variable_t variable);

neo_js_variable_t neo_js_context_to_boolean(neo_js_context_t ctx,
                                            neo_js_variable_t variable);

neo_js_variable_t neo_js_context_to_number(neo_js_context_t ctx,
                                           neo_js_variable_t variable);

neo_js_variable_t neo_js_context_is_equal(neo_js_context_t ctx,
                                          neo_js_variable_t variable,
                                          neo_js_variable_t another);

neo_js_variable_t neo_js_context_is_gt(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another);

neo_js_variable_t neo_js_context_is_lt(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another);

neo_js_variable_t neo_js_context_is_ge(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another);

neo_js_variable_t neo_js_context_is_le(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another);

neo_js_variable_t neo_js_context_add(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_sub(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_mul(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_div(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_mod(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_pow(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_not(neo_js_context_t ctx,
                                     neo_js_variable_t variable);

neo_js_variable_t neo_js_context_and(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_or(neo_js_context_t ctx,
                                    neo_js_variable_t variable,
                                    neo_js_variable_t another);

neo_js_variable_t neo_js_context_xor(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_shr(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_shl(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another);

neo_js_variable_t neo_js_context_ushr(neo_js_context_t ctx,
                                      neo_js_variable_t variable,
                                      neo_js_variable_t another);

neo_js_variable_t neo_js_context_inc(neo_js_context_t ctx,
                                     neo_js_variable_t variable);

neo_js_variable_t neo_js_context_dec(neo_js_context_t ctx,
                                     neo_js_variable_t variable);

neo_js_variable_t neo_js_context_logical_not(neo_js_context_t ctx,
                                             neo_js_variable_t variable);

neo_js_variable_t neo_js_context_concat(neo_js_context_t ctx,
                                        neo_js_variable_t variable,
                                        neo_js_variable_t another);

neo_js_variable_t neo_js_context_instance_of(neo_js_context_t ctx,
                                             neo_js_variable_t variable,
                                             neo_js_variable_t constructor);

neo_js_variable_t neo_js_context_eval(neo_js_context_t ctx, const char *file,
                                      const char *source);

#ifdef __cplusplus
}
#endif
#endif