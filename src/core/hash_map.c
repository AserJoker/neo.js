#include "neojs/core/hash_map.h"
#include "neojs/core/allocator.h"
#include "neojs/core/common.h"
#define NEO_HASH_MAP_BUCKET_SIZE 16

struct _neo_hash_map_node_t {
  neo_hash_map_node_t next;
  neo_hash_map_node_t last;
  void *key;
  void *value;
  bool manager;
};

static neo_hash_map_node_t neo_create_hash_map_node(neo_allocator_t allocator) {
  neo_hash_map_node_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_hash_map_node_t), NULL);
  node->key = NULL;
  node->value = NULL;
  node->manager = false;
  node->last = NULL;
  node->next = NULL;
  return node;
}

typedef struct _neo_hash_map_entry_t {
  struct _neo_hash_map_node_t head;
  struct _neo_hash_map_node_t tail;
} neo_hash_map_entry_t;

struct _neo_hash_map_t {
  bool auto_free_key;
  bool auto_free_value;
  neo_hash_fn_t hash;
  neo_compare_fn_t compare;
  uint32_t max_bucket;
  neo_hash_map_entry_t *buckets;
  neo_hash_map_node_t head;
  neo_hash_map_node_t tail;
  neo_allocator_t allocator;
  size_t size;
};

static void neo_hash_map_dispsoe(neo_allocator_t allocator,
                                 neo_hash_map_t self) {
  for (uint32_t idx = 0; idx < self->max_bucket; idx++) {
    neo_hash_map_entry_t *entity = &self->buckets[idx];
    while (entity->head.next != &entity->tail) {
      neo_hash_map_node_t node = entity->head.next;
      node->next->last = node->last;
      node->last->next = node->next;
      if (self->auto_free_key) {
        neo_allocator_free(allocator, node->key);
      }
      if (self->auto_free_value) {
        neo_allocator_free(allocator, node->value);
      }
      neo_allocator_free(allocator, node);
    }
  }
  neo_allocator_free(allocator, self->buckets);
}

neo_hash_map_t neo_create_hash_map(neo_allocator_t allocator,
                                   neo_hash_map_initialize_t *initialize) {
  neo_hash_map_t hmap = neo_allocator_alloc(
      allocator, sizeof(struct _neo_hash_map_t), neo_hash_map_dispsoe);
  hmap->allocator = allocator;
  if (initialize) {
    hmap->hash = initialize->hash;
    hmap->auto_free_key = initialize->auto_free_key;
    hmap->auto_free_value = initialize->auto_free_value;
    hmap->max_bucket = initialize->max_bucket;
    hmap->compare = initialize->compare;
  } else {
    hmap->max_bucket = NEO_HASH_MAP_BUCKET_SIZE;
    hmap->auto_free_key = false;
    hmap->auto_free_value = false;
    hmap->hash = NULL;
    hmap->compare = NULL;
  }
  if (hmap->max_bucket == 0) {
    hmap->max_bucket = NEO_HASH_MAP_BUCKET_SIZE;
  }
  hmap->buckets = neo_allocator_alloc(
      allocator, hmap->max_bucket * sizeof(neo_hash_map_entry_t), NULL);
  for (size_t index = 0; index < hmap->max_bucket; index++) {
    neo_hash_map_entry_t *entity = &hmap->buckets[index];
    entity->head.next = &entity->tail;
    entity->head.last = NULL;
    entity->head.value = NULL;
    entity->head.key = NULL;
    entity->head.manager = true;
    entity->tail.last = &entity->head;
    entity->tail.next = NULL;
    entity->tail.key = NULL;
    entity->tail.value = NULL;
    entity->tail.manager = true;
    if (index > 0) {
      neo_hash_map_entry_t *last = &hmap->buckets[index - 1];
      last->tail.next = &entity->head;
      entity->head.last = &last->tail;
    }
  }
  hmap->size = 0;
  hmap->head = &hmap->buckets[0].head;
  hmap->tail = &hmap->buckets[hmap->max_bucket - 1].tail;
  return hmap;
}

void neo_hash_map_set(neo_hash_map_t self, void *key, void *value) {
  neo_hash_map_node_t node = neo_hash_map_find(self, key);
  if (node) {
    if (self->auto_free_value && node->value) {
      neo_allocator_free(self->allocator, node->value);
    }
  } else {
    uint32_t hash = 0;
    if (self->hash) {
      hash = self->hash(key, self->max_bucket);
    } else {
      hash = ((uintptr_t)key) % self->max_bucket;
    }
    neo_hash_map_entry_t *bucket = &self->buckets[hash];
    node = neo_create_hash_map_node(self->allocator);
    node->next = &bucket->tail;
    node->last = bucket->tail.last;
    node->next->last = node;
    node->last->next = node;
    node->key = key;
    self->size++;
  }
  node->value = value;
}

void *neo_hash_map_get(neo_hash_map_t self, const void *key) {
  neo_hash_map_node_t node = neo_hash_map_find(self, key);
  if (node) {
    return node->value;
  } else {
    return NULL;
  }
}

bool neo_hash_map_has(neo_hash_map_t self, const void *key) {
  return neo_hash_map_find(self, key) != NULL;
}

void neo_hash_map_delete(neo_hash_map_t self, const void *key) {
  neo_hash_map_node_t node = neo_hash_map_find(self, key);
  neo_hash_map_erase(self, node);
}

void neo_hash_map_erase(neo_hash_map_t self, neo_hash_map_node_t position) {
  if (position && !position->manager) {
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

neo_hash_map_node_t neo_hash_map_find(neo_hash_map_t self, const void *key) {
  uint32_t hash = 0;
  if (self->hash) {
    hash = self->hash(key, self->max_bucket);
  } else {
    hash = ((uintptr_t)key) % self->max_bucket;
  }
  neo_hash_map_entry_t *bucket = &self->buckets[hash];
  neo_hash_map_node_t it = bucket->head.next;
  while (it != &bucket->tail) {
    if (self->compare) {
      if (self->compare(it->key, key) == 0) {
        break;
      }
    } else {
      if (it->key == key) {
        break;
      }
    }
    it = it->next;
  }
  if (it != &bucket->tail) {
    return it;
  } else {
    return NULL;
  }
}

size_t neo_hash_map_get_size(neo_hash_map_t self) { return self->size; }

neo_hash_map_node_t neo_hash_map_get_head(neo_hash_map_t self) {
  return self->head;
}

neo_hash_map_node_t neo_hash_map_get_tail(neo_hash_map_t self) {
  return self->tail;
}

neo_hash_map_node_t neo_hash_map_get_first(neo_hash_map_t self) {
  neo_hash_map_node_t head = self->head;
  while (head->manager && head != self->tail) {
    head = head->next;
  }
  return head;
}

neo_hash_map_node_t neo_hash_map_get_last(neo_hash_map_t self) {
  neo_hash_map_node_t tail = self->tail;
  while (tail->manager && tail != self->head) {
    tail = tail->last;
  }
  return tail;
}

void *neo_hash_map_node_get_key(neo_hash_map_node_t self) { return self->key; }

void *neo_hash_map_node_get_value(neo_hash_map_node_t self) {
  return self->value;
}

neo_hash_map_node_t neo_hash_map_node_next(neo_hash_map_node_t self) {
  self = self->next;
  while (self->next && self->manager) {
    self = self->next;
  }
  return self;
}

neo_hash_map_node_t neo_hash_map_node_last(neo_hash_map_node_t self) {
  self = self->last;
  while (self->last && self->manager) {
    self = self->last;
  }
  return self;
}
void neo_hash_map_clear(neo_hash_map_t self) {
  while (self->size) {
    neo_hash_map_erase(self, neo_hash_map_get_first(self));
  }
}