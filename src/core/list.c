#include "core/list.h"
#include "core/allocator.h"
#include <stddef.h>
struct _noix_list_node_t {
  noix_list_node_t next;
  noix_list_node_t last;
  void *data;
};

struct _noix_list_t {
  struct _noix_list_node_t begin;
  struct _noix_list_node_t tail;
  noix_allocator_t allocator;
  size_t size;
  bool autofree;
};

static void noix_list_node_dispose(noix_allocator_t allocator,
                                   noix_list_node_t self) {}

static noix_list_node_t noix_create_list_node(noix_allocator_t allocator,
                                              void *data) {
  noix_list_node_t node = noix_allocator_alloc(
      allocator, sizeof(struct _noix_list_node_t), noix_list_node_dispose);
  if (!node) {
    return NULL;
  }
  node->next = NULL;
  node->last = NULL;
  node->data = data;
  return node;
}

static void noix_list_dispose(noix_allocator_t allocator, noix_list_t self) {
  while (self->size > 0) {
    noix_list_erase(self, self->begin.next);
  }
}

noix_list_t noix_create_list(noix_allocator_t allocator,
                             noix_list_initialize *initialize) {
  noix_list_t list = noix_allocator_alloc(
      allocator, sizeof(struct _noix_list_t), noix_list_dispose);
  if (!list) {
    return NULL;
  }
  list->size = 0;
  list->begin.next = &list->tail;
  list->tail.last = &list->begin;
  list->begin.last = NULL;
  list->tail.next = NULL;
  list->allocator = allocator;
  if (initialize && initialize->autofree) {
    list->autofree = true;
  } else {
    list->autofree = false;
  }
  return list;
}

noix_list_node_t noix_list_get_head(noix_list_t self) { return &self->begin; }

noix_list_node_t noix_list_get_tail(noix_list_t self) { return &self->tail; }

size_t noix_list_get_size(noix_list_t self) { return self->size; }

noix_list_node_t noix_list_insert(noix_list_t self, noix_list_node_t position,
                                  void *data) {
  if (!position->next) {
    return NULL;
  }
  noix_list_node_t node = noix_create_list_node(self->allocator, data);
  node->last = position;
  node->next = position->next;
  position->next->last = node;
  position->next = node;
  self->size++;
  return node;
}

noix_list_node_t noix_list_erase(noix_list_t self, noix_list_node_t position) {
  if (!position->next || position->next == &self->tail || self->size == 0) {
    return NULL;
  }
  noix_list_node_t node = position->next;
  node->next->last = position;
  node->last->next = node->next;
  if (self->autofree) {
    noix_allocator_free(self->allocator, node->data);
  }
  noix_allocator_free(self->allocator, node);
  self->size--;
  return position->next;
}

noix_list_node_t noix_list_node_next(noix_list_node_t self) {
  return self->next;
}

noix_list_node_t noix_list_node_last(noix_list_node_t self) {
  return self->last;
}

void *noix_list_node_get(noix_list_node_t self) { return self->data; }