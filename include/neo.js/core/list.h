#ifndef _H_NEO_CORE_LIST_
#define _H_NEO_CORE_LIST_
#include "neo.js/core/common.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "neo.js/core/allocator.h"
#include <stdbool.h>
typedef struct _neo_list_t *neo_list_t;

typedef struct _neo_list_node_t *neo_list_node_t;

typedef struct _neo_list_initialize_t {
  bool auto_free;
} neo_list_initialize_t;

neo_list_t neo_create_list(neo_allocator_t allocator,
                           neo_list_initialize_t *initialize);

void *neo_list_at(neo_list_t self, size_t idx);

neo_list_node_t neo_list_get_head(neo_list_t self);

neo_list_node_t neo_list_get_tail(neo_list_t self);

neo_list_node_t neo_list_get_first(neo_list_t self);

neo_list_node_t neo_list_get_last(neo_list_t self);

size_t neo_list_get_size(neo_list_t self);

neo_list_node_t neo_list_insert(neo_list_t self, neo_list_node_t position,
                                void *data);

neo_list_node_t neo_list_push(neo_list_t self, void *data);

void neo_list_pop(neo_list_t self);

neo_list_node_t neo_list_unshift(neo_list_t self, void *data);

void neo_list_shift(neo_list_t self);

void neo_list_erase(neo_list_t self, neo_list_node_t position);

neo_list_node_t neo_list_find(neo_list_t self, void *item);

void neo_list_delete(neo_list_t self, void *item);

neo_list_node_t neo_list_node_next(neo_list_node_t self);

neo_list_node_t neo_list_node_last(neo_list_node_t self);

void *neo_list_node_get(neo_list_node_t self);

void neo_list_clear(neo_list_t self);

void neo_list_swap(neo_list_node_t a, neo_list_node_t b);

void neo_list_move(neo_list_t self, neo_list_node_t pos,
                   neo_list_node_t current);

void neo_list_sort(neo_list_node_t begin, neo_list_node_t end,
                   neo_compare_fn_t compare);

#ifdef __cplusplus
};
#endif
#endif