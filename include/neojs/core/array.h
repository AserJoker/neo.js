#ifndef _H_NEO_CORE_ARRAY_
#define _H_NEO_CORE_ARRAY_
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "neojs/core/allocator.h"
typedef struct _neo_array_t *neo_array_t;
typedef struct _neo_array_initialize_t {
  size_t capacity;
  bool autofree;
} neo_array_initialize_t;

neo_array_t neo_create_array(neo_allocator_t allocator,
                             neo_array_initialize_t *initialize);
void neo_array_resize(neo_array_t self, neo_allocator_t allocator, size_t size);
size_t neo_array_get_size(neo_array_t self);
size_t neo_array_get_capacity(neo_array_t self);
void neo_array_strink_to_fit(neo_array_t self, neo_allocator_t allocator);
void neo_array_set_index(neo_array_t self, neo_allocator_t allocator,
                         size_t idx, void *data);
void *neo_array_get_index(neo_array_t self, neo_allocator_t allocator,
                          size_t idx);
void neo_array_push_back(neo_array_t self, neo_allocator_t allocator,
                         void *data);
void *neo_array_back(neo_array_t self);
void neo_array_pop_back(neo_array_t self, neo_allocator_t allocator);

#ifdef __cplusplus
};
#endif
#endif