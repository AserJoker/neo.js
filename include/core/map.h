#ifndef _H_NOIX_CORE_MAP_
#define _H_NOIX_CORE_MAP_
#ifdef __cplusplus
extern "C" {
#endif
#include "core/allocator.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct _noix_map_t *noix_map_t;

typedef struct _noix_map_node_t *noix_map_node_t;

typedef struct _noix_map_initialize {
  bool auto_free_key;
  bool auto_free_value;
  noix_compare_fn_t compare;
} noix_map_initialize;

noix_map_t noix_create_map(noix_allocator_t allocator,
                           noix_map_initialize *initialize);

void noix_map_set(noix_map_t self, void *key, void *value);

void *noix_map_get(noix_map_t self, void *key);

bool noix_map_has(noix_map_t self, void *key);

void noix_map_delete(noix_map_t self, void *key);

void noix_map_erase(noix_map_t self, noix_map_node_t position);

noix_map_node_t noix_map_find(noix_map_t self, void *key);

size_t noix_map_get_size(noix_map_t self);

noix_map_node_t noix_map_get_head(noix_map_t self);

noix_map_node_t noix_map_get_tail(noix_map_t self);

noix_map_node_t noix_map_get_first(noix_map_t self);

noix_map_node_t noix_map_get_last(noix_map_t self);

void *noix_map_node_get_key(noix_map_node_t self);

void *noix_map_node_get_value(noix_map_node_t self);

noix_map_node_t noix_map_node_next(noix_map_node_t self);

noix_map_node_t noix_map_node_last(noix_map_node_t self);

#ifdef __cplusplus
};
#endif
#endif