#ifndef _H_NEO_CORE_BUFFER_
#define _H_NEO_CORE_BUFFER_
#ifdef __cplusplus
extern "C" {
#endif
#include "neo.js/core/allocator.h"
typedef struct _neo_buffer_t *neo_buffer_t;
neo_buffer_t neo_create_buffer(neo_allocator_t allocator, size_t align);
size_t neo_buffer_get_size(neo_buffer_t self);
size_t neo_buffer_get_capacity(neo_buffer_t self);
void neo_buffer_shrink_to_fit(neo_buffer_t self);
void neo_buffer_reserve(neo_buffer_t self, size_t capacity);
void *neo_buffer_get(neo_buffer_t self);
void neo_buffer_write(neo_buffer_t self, size_t offset, void *data,
                      size_t size);
#ifdef __cplusplus
};
#endif
#endif