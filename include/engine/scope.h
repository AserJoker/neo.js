#ifndef _H_NEO_ENGINE_SCOPE_
#define _H_NEO_ENGINE_SCOPE_
#include "core/allocator.h"
#include "engine/chunk.h"
#include "engine/type.h"
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_scope_t *neo_js_scope_t;

neo_js_scope_t neo_create_js_scope(neo_allocator_t allocator,
                                   neo_js_scope_t parent);

neo_js_scope_t neo_js_scope_get_parent(neo_js_scope_t self);

neo_js_scope_t neo_js_scope_set_parent(neo_js_scope_t self,
                                       neo_js_scope_t parent);

neo_js_chunk_t neo_js_scope_get_rootneo_create_js_chunk(neo_js_scope_t self);

neo_js_variable_t neo_js_scope_get_variable(neo_js_scope_t self,
                                            const wchar_t *name);

void neo_js_scope_set_variable(neo_js_scope_t self, neo_js_variable_t variable,
                               const wchar_t *name);

neo_js_variable_t neo_js_scope_create_variable(neo_js_scope_t self,
                                               neo_js_chunk_t handle,
                                               const wchar_t *name);

neo_list_t neo_js_scope_get_variables(neo_js_scope_t scope);

void neo_js_scope_defer_free(neo_js_scope_t scope, void *data);
#ifdef __cplusplus
}
#endif
#endif