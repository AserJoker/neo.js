#include "core/map.h"
#include "core/allocator.h"
#include <stdint.h>
struct _noix_map_node_t {
  void *key;
  void *value;
  noix_map_node_t next;
  noix_map_node_t last;
};

struct _noix_map_t {
  bool auto_free_key;
  bool auto_free_value;
  struct _noix_map_node_t head;
  struct _noix_map_node_t tail;
  noix_allocator_t allocator;
  noix_compare_fn_t compare;
  size_t size;
};

static void noix_map_dispose(noix_allocator_t allocator, noix_map_t self) {
  noix_map_node_t item = self->head.next;
  while (self->size) {
    noix_map_erase(self, self->head.next);
  }
}

static int8_t noix_map_compare(void *a, void *b) {
  if ((intptr_t)a - (intptr_t)b) {
    return 1;
  }
  if ((intptr_t)b - (intptr_t)a) {
    return -1;
  }
  return 0;
}

noix_map_t noix_create_map(noix_allocator_t allocator,
                           noix_map_initialize *initialize) {
  noix_map_t map =
      (noix_map_t)noix_allocator_alloc(allocator, sizeof(struct _noix_map_t),
                                       (noix_destructor_fn_t)noix_map_dispose);
  map->allocator = allocator;
  map->size = 0;
  if (initialize) {
    map->auto_free_key = initialize->auto_free_key;
    map->auto_free_value = initialize->auto_free_value;
    map->compare = initialize->compare;
  } else {
    map->auto_free_value = false;
    map->auto_free_key = false;
    map->compare = &noix_map_compare;
  }
  map->head.next = &map->tail;
  map->tail.last = &map->head;
  map->head.last = NULL;
  map->tail.next = NULL;
  return map;
}

void noix_map_set(noix_map_t self, void *key, void *value) {
  noix_map_node_t node = noix_map_find(self, key);
  if (!node) {
    node = (noix_map_node_t)noix_allocator_alloc(
        self->allocator, sizeof(struct _noix_map_node_t), NULL);
    node->last = self->tail.last;
    node->next = &self->tail;
    node->last->next = node;
    node->next->last = node;
    node->key = key;
    node->value = value;
    self->size++;
    return;
  }
  if (node->value != value) {
    if (self->auto_free_value) {
      noix_allocator_free(self->allocator, node->value);
    }
    node->value = value;
  }
}

void *noix_map_get(noix_map_t self, void *key) {
  noix_map_node_t node = noix_map_find(self, key);
  if (node) {
    return node->value;
  }
  return NULL;
}

bool noix_map_has(noix_map_t self, void *key) {
  return noix_map_find(self, key) != NULL;
}

void noix_map_delete(noix_map_t self, void *key) {
  noix_map_node_t node = noix_map_find(self, key);
  noix_map_erase(self, node);
}

void noix_map_erase(noix_map_t self, noix_map_node_t position) {
  if (position) {
    position->last->next = position->next;
    position->next->last = position->last;
    if (self->auto_free_key) {
      noix_allocator_free(self->allocator, position->key);
    }
    if (self->auto_free_value) {
      noix_allocator_free(self->allocator, position->value);
    }
    noix_allocator_free(self->allocator, position);
    self->size--;
  }
}

noix_map_node_t noix_map_find(noix_map_t self, void *key) {
  noix_map_node_t node = self->head.next;
  while (node != &self->tail) {
    if (self->compare(node->key, key) == 0) {
      return node;
    }
    node = node->next;
  }
  return NULL;
}

size_t noix_map_get_size(noix_map_t self) { return self->size; }

noix_map_node_t noix_map_get_head(noix_map_t self) { return &self->head; }

noix_map_node_t noix_map_get_tail(noix_map_t self) { return &self->tail; }

noix_map_node_t noix_map_get_first(noix_map_t self) {
  if (!self->size) {
    return NULL;
  }
  return self->head.next;
}

noix_map_node_t noix_map_get_last(noix_map_t self) {
  if (!self->size) {
    return NULL;
  }
  return self->tail.last;
}

void *noix_map_node_get_key(noix_map_node_t self) { return self->key; }

void *noix_map_node_get_value(noix_map_node_t self) { return self->value; }

noix_map_node_t noix_map_node_next(noix_map_node_t self) { return self->next; }

noix_map_node_t noix_map_node_last(noix_map_node_t self) { return self->last; }
