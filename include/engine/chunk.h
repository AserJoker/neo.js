#ifndef _H_NEO_ENGINE_CHUNK_
#define _H_NEO_ENGINE_CHUNK_
#include "core/allocator.h"
#include "core/list.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_chunk_t *neo_js_chunk_t;

neo_js_chunk_t neo_create_js_chunk(neo_allocator_t allocator,
                                   neo_js_value_t value);

neo_js_value_t neo_js_chunk_get_value(neo_js_chunk_t self);

void neo_js_chunk_set_value(neo_allocator_t allocator, neo_js_chunk_t self,
                            neo_js_value_t value);

void neo_js_chunk_add_parent(neo_js_chunk_t self, neo_js_chunk_t parent);

void neo_js_chunk_remove_parent(neo_js_chunk_t self, neo_js_chunk_t parent);

neo_list_t neo_js_chunk_get_parents(neo_js_chunk_t self);

neo_list_t neo_js_chunk_get_children(neo_js_chunk_t self);

bool neo_js_chunk_check_alive(neo_allocator_t allocator, neo_js_chunk_t handle);

void neo_js_chunk_gc(neo_allocator_t allocator, neo_js_chunk_t root);

size_t *neo_js_chunk_get_ref(neo_js_chunk_t self);

#ifdef __cplusplus
}
#endif
#endif