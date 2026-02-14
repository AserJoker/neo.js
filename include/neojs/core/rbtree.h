#ifndef _H_NEO_CORE_RBTREE_
#define _H_NEO_CORE_RBTREE_
#include "neojs/core/common.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "neojs/core/allocator.h"
typedef struct _neo_rbtree_node_t *neo_rbtree_node_t;
typedef struct _neo_rbtree_t *neo_rbtree_t;
typedef struct _neo_rbtree_initialize_t {
  bool autofree;
  neo_compare_fn_t compare;
} neo_rbtree_initialize_t;
neo_rbtree_t neo_create_rbtree(neo_allocator_t allocator,
                               neo_rbtree_initialize_t *initialize);
void neo_rbtree_put(neo_rbtree_t self, void *key);
bool neo_rbtree_has(neo_rbtree_t self, const void *key);
void neo_rbtree_remove(neo_rbtree_t self, const void *key);
size_t neo_rbtree_size(neo_rbtree_t self);
neo_rbtree_node_t neo_rbtree_get_first(neo_rbtree_t self);
neo_rbtree_node_t neo_rbtree_get_last(neo_rbtree_t self);
neo_rbtree_node_t neo_rbtree_node_next(neo_rbtree_node_t self);
neo_rbtree_node_t neo_rbtree_node_last(neo_rbtree_node_t self);
void *neo_rbtree_node_get(neo_rbtree_node_t self);
#ifdef __cplusplus
};
#endif
#endif