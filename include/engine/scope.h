#ifndef _H_NEO_ENGINE_SCOPE_
#define _H_NEO_ENGINE_SCOPE_
#include "core/allocator.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_engine_scope_t *neo_engine_scope_t;

neo_engine_scope_t neo_create_js_scope(neo_allocator_t allocator,
                                       neo_engine_scope_t parent);

uint32_t neo_engine_scope_add_ref(neo_engine_scope_t self);

uint32_t neo_engine_scope_ref(neo_engine_scope_t self);

uint32_t neo_engine_scope_release(neo_engine_scope_t self);

neo_engine_scope_t neo_engine_scope_get_parent(neo_engine_scope_t self);

neo_engine_scope_t neo_engine_scope_set_parent(neo_engine_scope_t self,
                                               neo_engine_scope_t parent);

neo_engine_handle_t neo_engine_scope_get_root_handle(neo_engine_scope_t self);

neo_engine_variable_t neo_engine_scope_get_variable(neo_engine_scope_t self,
                                                    const wchar_t *name);

void neo_engine_scope_set_variable(neo_allocator_t allocator,
                                   neo_engine_scope_t self,
                                   neo_engine_variable_t variable,
                                   const wchar_t *name);

neo_engine_variable_t
neo_engine_scope_create_variable(neo_allocator_t allocator,
                                 neo_engine_scope_t self,
                                 neo_engine_handle_t handle);
#ifdef __cplusplus
}
#endif
#endif