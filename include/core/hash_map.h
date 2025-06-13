#ifndef _H_NEO_CORE_HASH_MAP_
#define _H_NEO_CORE_HASH_MAP_
#ifdef __cplusplus
extern "C" {
#endif
#include "core/allocator.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct _neo_hash_map_t *neo_hash_map_t;

typedef struct _neo_hash_map_node_t *neo_hash_map_node_t;

typedef struct _neo_hash_map_initialize {
  bool auto_free_key;
  bool auto_free_value;
  neo_compare_fn_t compare;
  neo_hash_fn_t hash;
  uint32_t max_bucket;
} neo_hash_map_initialize_t;

neo_hash_map_t neo_create_hash_map(neo_allocator_t allocator,
                                   neo_hash_map_initialize_t *initialize);

void neo_hash_map_set(neo_hash_map_t self, void *key, void *value,
                      void *cmp_arg, void *hash_arg);

void *neo_hash_map_get(neo_hash_map_t self, const void *key, void *cmp_arg,
                       void *hash_arg);

bool neo_hash_map_has(neo_hash_map_t self, void *key, void *cmp_arg,
                      void *hash_arg);

void neo_hash_map_delete(neo_hash_map_t self, void *key, void *cmp_arg,
                         void *hash_arg);

void neo_hash_map_erase(neo_hash_map_t self, neo_hash_map_node_t position);

neo_hash_map_node_t neo_hash_map_find(neo_hash_map_t self, const void *key,
                                      void *cmp_arg, void *hash_arg);

size_t neo_hash_map_get_size(neo_hash_map_t self);

neo_hash_map_node_t neo_hash_map_get_head(neo_hash_map_t self);

neo_hash_map_node_t neo_hash_map_get_tail(neo_hash_map_t self);

neo_hash_map_node_t neo_hash_map_get_first(neo_hash_map_t self);

neo_hash_map_node_t neo_hash_map_get_last(neo_hash_map_t self);

void *neo_hash_map_node_get_key(neo_hash_map_node_t self);

void *neo_hash_map_node_get_value(neo_hash_map_node_t self);

neo_hash_map_node_t neo_hash_map_node_next(neo_hash_map_node_t self);

neo_hash_map_node_t neo_hash_map_node_last(neo_hash_map_node_t self);

#ifdef __cplusplus
};
#endif
#endif