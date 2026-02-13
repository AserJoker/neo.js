#include "neojs/core/array.h"
#include "neojs/core/allocator.h"
struct _neo_array_t {
  size_t size;
  size_t capacity;
  void **data;
  bool autofree;
};
static void neo_array_dispose(neo_array_t self, neo_allocator_t allcoator) {}
neo_array_t neo_create_array(neo_allocator_t allocator,
                             neo_array_initialize_t *initialize) {
  neo_array_t array = neo_allocator_alloc(
      allocator, sizeof(struct _neo_array_t), neo_array_dispose);
  array->autofree = false;
  array->capacity = 0;
  array->size = 0;
  array->data = NULL;
  return array;
}
void neo_array_resize(neo_array_t self, neo_allocator_t allocator,
                      size_t size) {
  if (size > self->capacity) {
    self->capacity = size;
    void **data =
        neo_allocator_alloc(allocator, sizeof(void *) * self->capacity, NULL);
    for (size_t idx = 0; idx < self->size; idx++) {
      data[idx] = self->data[idx];
    }
    neo_allocator_free(allocator, self->data);
    self->data = data;
  }
  for (size_t idx = size; idx < self->size; idx++) {
    if (self->autofree) {
      neo_allocator_free(allocator, self->data[idx]);
    } else {
      self->data[idx] = NULL;
    }
  }
  for (size_t idx = self->size; idx < size; idx++) {
    self->data[idx] = NULL;
  }
  self->size = size;
}
size_t neo_array_get_size(neo_array_t self) { return self->size; }
size_t neo_array_get_capacity(neo_array_t self) { return self->capacity; }
void neo_array_strink_to_fit(neo_array_t self, neo_allocator_t allocator) {
  if (self->size != self->capacity) {
    neo_array_resize(self, allocator, self->size);
  }
}
void neo_array_set_index(neo_array_t self, neo_allocator_t allocator,
                         size_t idx, void *data) {
  if (idx < self->size && self->data[idx] != data) {
    if (self->autofree) {
      neo_allocator_free(allocator, self->data[idx]);
    }
    self->data[idx] = data;
  }
}
void *neo_array_get_index(neo_array_t self, neo_allocator_t allocator,
                          size_t idx) {
  if (idx < self->size) {
    return self->data[idx];
  }
  return NULL;
}
void neo_array_push_back(neo_array_t self, neo_allocator_t allocator,
                         void *data) {
  if (self->size >= self->capacity) {
    neo_array_resize(self, allocator, self->capacity * 2);
  }
  self->data[self->size] = data;
  self->size++;
}
void *neo_array_back(neo_array_t self) {
  if (self->size) {
    return self->data[self->size - 1];
  }
  return NULL;
}
void neo_array_pop_back(neo_array_t self, neo_allocator_t allocator) {
  if (self->size && self->autofree) {
    neo_allocator_free(allocator, self->data[self->size - 1]);
  }
  self->data[self->size - 1] = NULL;
  self->size--;
}
