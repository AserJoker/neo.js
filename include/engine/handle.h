#ifndef _H_NEO_ENGINE_HANDLE_
#define _H_NEO_ENGINE_HANDLE_
#include "core/allocator.h"
#include "engine/chunk.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_handle_t *neo_js_handle_t;

neo_js_handle_t neo_create_js_handle(neo_allocator_t allocator,
                                     neo_js_chunk_t chunk);

neo_js_chunk_t neo_js_handle_get_chunk(neo_js_handle_t self);

void neo_js_handle_set_chunk(neo_js_handle_t self, neo_js_chunk_t chunk);

size_t *neo_js_handle_get_ref(neo_js_handle_t self);

size_t neo_js_handle_add_ref(neo_js_handle_t self);
size_t neo_js_handle_release(neo_js_handle_t self);

#ifdef __cplusplus
}
#endif
#endif