#ifndef _H_NEO_CORE_MAP_
#define _H_NEO_CORE_MAP_
#ifdef __cplusplus
extern "C" {
#endif
#include "neojs/core/allocator.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct _neo_map_t *neo_map_t;

typedef struct _neo_map_node_t *neo_map_node_t;

typedef struct _neo_map_initialize {
  bool auto_free_key;
  bool auto_free_value;
  neo_compare_fn_t compare;
} neo_map_initialize_t;

neo_map_t neo_create_map(neo_allocator_t allocator,
                         neo_map_initialize_t *initialize);

void neo_map_set(neo_map_t self, void *key, void *value);

void *neo_map_get(neo_map_t self, const void *key);

bool neo_map_has(neo_map_t self, void *key);

void neo_map_delete(neo_map_t self, void *key);

void neo_map_erase(neo_map_t self, neo_map_node_t position);

neo_map_node_t neo_map_find(neo_map_t self, const void *key);

size_t neo_map_get_size(neo_map_t self);

neo_map_node_t neo_map_get_head(neo_map_t self);

neo_map_node_t neo_map_get_tail(neo_map_t self);

neo_map_node_t neo_map_get_first(neo_map_t self);

neo_map_node_t neo_map_get_last(neo_map_t self);

void *neo_map_node_get_key(neo_map_node_t self);

void *neo_map_node_get_value(neo_map_node_t self);

neo_map_node_t neo_map_node_next(neo_map_node_t self);

neo_map_node_t neo_map_node_last(neo_map_node_t self);

#ifdef __cplusplus
};
#endif
#endif