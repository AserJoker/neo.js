#ifndef _H_NOIX_CORE_LIST_
#define _H_NOIX_CORE_LIST_
#ifdef __cplusplus
extern "C" {
#endif
#include "core/allocator.h"
#include <stdbool.h>
typedef struct _noix_list_t *noix_list_t;

typedef struct _noix_list_node_t *noix_list_node_t;

typedef struct _noix_list_initialize_t {
  bool auto_free;
} noix_list_initialize_t;

noix_list_t noix_create_list(noix_allocator_t allocator,
                             noix_list_initialize_t *initialize);

void *noix_list_at(noix_list_t self, size_t idx);

noix_list_node_t noix_list_get_head(noix_list_t self);

noix_list_node_t noix_list_get_tail(noix_list_t self);

noix_list_node_t noix_list_get_first(noix_list_t self);

noix_list_node_t noix_list_get_last(noix_list_t self);

size_t noix_list_get_size(noix_list_t self);

noix_list_node_t noix_list_insert(noix_list_t self, noix_list_node_t position,
                                  void *data);

noix_list_node_t noix_list_push(noix_list_t self, void *data);

void noix_list_pop(noix_list_t self);

noix_list_node_t noix_list_unshift(noix_list_t self, void *data);

void noix_list_shift(noix_list_t self);

noix_list_node_t noix_list_erase(noix_list_t self, noix_list_node_t position);

noix_list_node_t noix_list_node_next(noix_list_node_t self);

noix_list_node_t noix_list_node_last(noix_list_node_t self);

void *noix_list_node_get(noix_list_node_t self);

#ifdef __cplusplus
};
#endif
#endif