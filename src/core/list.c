#include "core/list.h"
#include "core/allocator.h"
#include <stddef.h>
struct _noix_list_node_t {
  noix_list_node_t next;
  noix_list_node_t last;
  void *data;
};

struct _noix_list_t {
  struct _noix_list_node_t head;
  struct _noix_list_node_t tail;
  noix_allocator_t allocator;
  size_t size;
  bool auto_free;
};

static noix_list_node_t noix_create_list_node(noix_allocator_t allocator,
                                              void *data) {
  noix_list_node_t node =
      noix_allocator_alloc(allocator, sizeof(struct _noix_list_node_t), NULL);
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
    noix_list_erase(self, self->head.next);
  }
}

noix_list_t noix_create_list(noix_allocator_t allocator,
                             noix_list_initialize_t *initialize) {
  noix_list_t list = noix_allocator_alloc(
      allocator, sizeof(struct _noix_list_t), noix_list_dispose);
  if (!list) {
    return NULL;
  }
  list->size = 0;
  list->head.next = &list->tail;
  list->tail.last = &list->head;
  list->head.last = NULL;
  list->tail.next = NULL;
  list->allocator = allocator;
  if (initialize && initialize->auto_free) {
    list->auto_free = true;
  } else {
    list->auto_free = false;
  }
  return list;
}

void *noix_list_at(noix_list_t self, size_t idx) {
  if (idx >= self->size) {
    return NULL;
  }
  noix_list_node_t node = self->head.next;
  while (idx > 0) {
    node = node->next;
    idx--;
  }
  return node->data;
}

noix_list_node_t noix_list_get_head(noix_list_t self) { return &self->head; }

noix_list_node_t noix_list_get_tail(noix_list_t self) { return &self->tail; }

noix_list_node_t noix_list_get_first(noix_list_t self) {
  if (self->size) {
    return self->head.next;
  }
  return NULL;
}

noix_list_node_t noix_list_get_last(noix_list_t self) {
  if (self->size) {
    return self->tail.last;
  }
  return NULL;
}

size_t noix_list_get_size(noix_list_t self) { return self->size; }

noix_list_node_t noix_list_insert(noix_list_t self, noix_list_node_t position,
                                  void *data) {
  if (!position->next) {
    return NULL;
  }
  noix_list_node_t node = noix_create_list_node(self->allocator, data);
  node->last = position;
  node->next = position->next;
  node->next->last = node;
  node->last->next = node;
  self->size++;
  return node;
}

noix_list_node_t noix_list_push(noix_list_t self, void *data) {
  return noix_list_insert(self, self->tail.last, data);
}

void noix_list_pop(noix_list_t self) {
  if (self->size) {
    noix_list_erase(self, self->tail.last);
  }
}

noix_list_node_t noix_list_unshift(noix_list_t self, void *data) {
  return noix_list_insert(self, &self->head, data);
}

void noix_list_shift(noix_list_t self) {
  if (self->size) {
    noix_list_erase(self, self->head.next);
  }
}

noix_list_node_t noix_list_erase(noix_list_t self, noix_list_node_t position) {
  if (self->size == 0) {
    return NULL;
  }
  position->last->next = position->next;
  position->next->last = position->last;
  if (self->auto_free) {
    noix_allocator_free(self->allocator, position->data);
  }
  noix_allocator_free(self->allocator, position);
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