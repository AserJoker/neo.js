#include "core/map.h"
#include "core/allocator.h"
#include <stdint.h>
struct _neo_map_node_t {
  void *key;
  void *value;
  neo_map_node_t next;
  neo_map_node_t last;
};

struct _neo_map_t {
  bool auto_free_key;
  bool auto_free_value;
  struct _neo_map_node_t head;
  struct _neo_map_node_t tail;
  neo_allocator_t allocator;
  neo_compare_fn_t compare;
  size_t size;
};

static void neo_map_dispose(neo_allocator_t allocator, neo_map_t self) {
  neo_map_node_t item = self->head.next;
  while (self->size) {
    neo_map_erase(self, self->head.next);
  }
}

neo_map_t neo_create_map(neo_allocator_t allocator,
                         neo_map_initialize_t *initialize) {
  neo_map_t map =
      (neo_map_t)neo_allocator_alloc(allocator, sizeof(struct _neo_map_t),
                                     (neo_destructor_fn_t)neo_map_dispose);
  map->allocator = allocator;
  map->size = 0;
  if (initialize) {
    map->auto_free_key = initialize->auto_free_key;
    map->auto_free_value = initialize->auto_free_value;
    map->compare = initialize->compare;
  } else {
    map->auto_free_value = false;
    map->auto_free_key = false;
  }
  if (!map->compare) {
  }
  map->head.next = &map->tail;
  map->tail.last = &map->head;
  map->head.last = NULL;
  map->tail.next = NULL;
  return map;
}

void neo_map_set(neo_map_t self, void *key, void *value, void *arg) {
  neo_map_node_t node = neo_map_find(self, key, arg);
  if (!node) {
    node = (neo_map_node_t)neo_allocator_alloc(
        self->allocator, sizeof(struct _neo_map_node_t), NULL);
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
      neo_allocator_free(self->allocator, node->value);
    }
    node->value = value;
  }
}

void *neo_map_get(neo_map_t self, const void *key, void *arg) {
  neo_map_node_t node = neo_map_find(self, key, arg);
  if (node) {
    return node->value;
  }
  return NULL;
}

bool neo_map_has(neo_map_t self, void *key, void *arg) {
  return neo_map_find(self, key, arg) != NULL;
}

void neo_map_delete(neo_map_t self, void *key, void *arg) {
  neo_map_node_t node = neo_map_find(self, key, arg);
  if (node) {
    neo_map_erase(self, node);
  }
}

void neo_map_erase(neo_map_t self, neo_map_node_t position) {
  if (position) {
    position->last->next = position->next;
    position->next->last = position->last;
    if (self->auto_free_key) {
      neo_allocator_free(self->allocator, position->key);
    }
    if (self->auto_free_value) {
      neo_allocator_free(self->allocator, position->value);
    }
    neo_allocator_free(self->allocator, position);
    self->size--;
  }
}

neo_map_node_t neo_map_find(neo_map_t self, const void *key, void *arg) {
  neo_map_node_t node = self->head.next;
  while (node != &self->tail) {
    if (self->compare) {
      if (self->compare(node->key, key, arg) == 0) {
        return node;
      } else {
        if (node->key == key) {
          return node;
        }
      }
    }
    node = node->next;
  }
  return NULL;
}

size_t neo_map_get_size(neo_map_t self) { return self->size; }

neo_map_node_t neo_map_get_head(neo_map_t self) { return &self->head; }

neo_map_node_t neo_map_get_tail(neo_map_t self) { return &self->tail; }

neo_map_node_t neo_map_get_first(neo_map_t self) {
  if (!self->size) {
    return NULL;
  }
  return self->head.next;
}

neo_map_node_t neo_map_get_last(neo_map_t self) {
  if (!self->size) {
    return NULL;
  }
  return self->tail.last;
}

void *neo_map_node_get_key(neo_map_node_t self) { return self->key; }

void *neo_map_node_get_value(neo_map_node_t self) { return self->value; }

neo_map_node_t neo_map_node_next(neo_map_node_t self) { return self->next; }

neo_map_node_t neo_map_node_last(neo_map_node_t self) { return self->last; }
