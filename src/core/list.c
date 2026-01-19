#include "neojs/core/list.h"
#include "neojs/core/allocator.h"
#include <stddef.h>
struct _neo_list_node_t {
  neo_list_node_t next;
  neo_list_node_t last;
  void *data;
};

struct _neo_list_t {
  struct _neo_list_node_t head;
  struct _neo_list_node_t tail;
  neo_allocator_t allocator;
  size_t size;
  bool auto_free;
};

static neo_list_node_t neo_create_list_node(neo_allocator_t allocator,
                                            void *data) {
  neo_list_node_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_list_node_t), NULL);
  if (!node) {
    return NULL;
  }
  node->next = NULL;
  node->last = NULL;
  node->data = data;
  return node;
}

static void neo_list_dispose(neo_allocator_t allocator, neo_list_t self) {
  while (self->size > 0) {
    neo_list_erase(self, self->head.next);
  }
}

neo_list_t neo_create_list(neo_allocator_t allocator,
                           neo_list_initialize_t *initialize) {
  neo_list_t list = neo_allocator_alloc(allocator, sizeof(struct _neo_list_t),
                                        neo_list_dispose);
  if (!list) {
    return NULL;
  }
  list->size = 0;
  list->head.next = &list->tail;
  list->tail.last = &list->head;
  list->head.last = NULL;
  list->tail.next = NULL;
  list->head.data = NULL;
  list->tail.data = NULL;
  list->allocator = allocator;
  if (initialize && initialize->auto_free) {
    list->auto_free = true;
  } else {
    list->auto_free = false;
  }
  return list;
}

void *neo_list_at(neo_list_t self, size_t idx) {
  if (idx >= self->size) {
    return NULL;
  }
  neo_list_node_t node = self->head.next;
  while (idx > 0) {
    node = node->next;
    idx--;
  }
  return node->data;
}

neo_list_node_t neo_list_get_head(neo_list_t self) { return &self->head; }

neo_list_node_t neo_list_get_tail(neo_list_t self) { return &self->tail; }

neo_list_node_t neo_list_get_first(neo_list_t self) { return self->head.next; }

neo_list_node_t neo_list_get_last(neo_list_t self) { return self->tail.last; }

size_t neo_list_get_size(neo_list_t self) { return self->size; }

neo_list_node_t neo_list_insert(neo_list_t self, neo_list_node_t position,
                                void *data) {
  if (!position->next) {
    return NULL;
  }
  neo_list_node_t node = neo_create_list_node(self->allocator, data);
  node->last = position;
  node->next = position->next;
  node->next->last = node;
  node->last->next = node;
  self->size++;
  return node;
}

neo_list_node_t neo_list_push(neo_list_t self, void *data) {
  return neo_list_insert(self, self->tail.last, data);
}

void neo_list_pop(neo_list_t self) {
  if (self->size) {
    neo_list_erase(self, self->tail.last);
  }
}

neo_list_node_t neo_list_unshift(neo_list_t self, void *data) {
  return neo_list_insert(self, &self->head, data);
}

void neo_list_shift(neo_list_t self) {
  if (self->size) {
    neo_list_erase(self, self->head.next);
  }
}

void neo_list_erase(neo_list_t self, neo_list_node_t position) {
  if (self->size == 0) {
    return;
  }
  position->last->next = position->next;
  position->next->last = position->last;
  if (self->auto_free) {
    neo_allocator_free(self->allocator, position->data);
  }
  neo_allocator_free(self->allocator, position);
  self->size--;
  return;
}

neo_list_node_t neo_list_find(neo_list_t self, void *item) {
  for (neo_list_node_t it = self->head.next; it != &self->tail; it = it->next) {
    if (it->data == item) {
      return it;
    }
  }
  return NULL;
}

void neo_list_delete(neo_list_t self, void *item) {
  neo_list_node_t it = neo_list_find(self, item);
  if (it) {
    neo_list_erase(self, it);
  }
}

neo_list_node_t neo_list_node_next(neo_list_node_t self) { return self->next; }

neo_list_node_t neo_list_node_last(neo_list_node_t self) { return self->last; }

void *neo_list_node_get(neo_list_node_t self) { return self->data; }
void neo_list_clear(neo_list_t self) {
  while (self->size) {
    neo_list_erase(self, self->head.next);
  }
}
void neo_list_swap(neo_list_node_t a, neo_list_node_t b) {
  void *dataa = a->data;
  void *datab = b->data;
  b->data = dataa;
  a->data = datab;
}

void neo_list_move(neo_list_t self, neo_list_node_t pos,
                   neo_list_node_t current) {
  if (pos == current) {
    return;
  }
  current->last->next = current->next;
  current->next->last = current->last;

  current->last = pos;
  current->next = pos->next;

  current->next->last = current;
  current->last->next = current;
}

void neo_list_sort(neo_list_node_t begin, neo_list_node_t end,
                   neo_compare_fn_t compare) {
  if (begin == end) {
    return;
  }
  neo_list_node_t flag = begin;
  begin = begin->next;
  neo_list_node_t stop = end->next;
  neo_list_node_t start = flag->last;
  for (; begin != stop; begin = begin->next) {
    if (compare(begin->data, flag->data) < 0) {
      begin->next->last = begin->last;
      begin->last->next = begin->next;

      begin->last = flag->last;
      begin->next = flag;
      begin->last->next = begin;
      begin->next->last = begin;
    }
  }
  if (start != flag->last) {
    neo_list_sort(start->next, flag->last, compare);
  }
  if (end != flag->next) {
    neo_list_sort(flag->next, stop->last, compare);
  }
}