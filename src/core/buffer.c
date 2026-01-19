#include "neojs/core/buffer.h"
#include "neojs/core/allocator.h"
#include <string.h>
struct _neo_buffer_t {
  void *data;
  size_t size;
  size_t capacity;
  size_t align;
  neo_allocator_t allocator;
};
static void neo_buffer_dispose(neo_allocator_t allocator, neo_buffer_t self) {
  neo_allocator_free(allocator, self->data);
}
neo_buffer_t neo_create_buffer(neo_allocator_t allocator, size_t align) {
  neo_buffer_t buffer = neo_allocator_alloc(
      allocator, sizeof(struct _neo_buffer_t), neo_buffer_dispose);
  buffer->data = NULL;
  buffer->size = 0;
  buffer->capacity = 0;
  buffer->allocator = allocator;
  buffer->align = align;
  if (!buffer->align) {
    buffer->align = 1;
  }
  return buffer;
}
size_t neo_buffer_get_size(neo_buffer_t self) { return self->size; }
size_t neo_buffer_get_capacity(neo_buffer_t self) { return self->capacity; }
void neo_buffer_shrink_to_fit(neo_buffer_t self) {
  if (self->size != self->capacity) {
    size_t size = self->size;
    if (size % self->align != 0) {
      size = size / self->align + 1;
      size = size * self->align;
    }
    void *data = neo_allocator_alloc(self->allocator, size, NULL);
    memset(data, 0, size);
    memcpy(data, self->data, self->size);
    neo_allocator_free(self->allocator, self->data);
    self->data = data;
    self->capacity = size;
  }
}
void neo_buffer_reserve(neo_buffer_t self, size_t capacity) {
  size_t size = capacity;
  if (size % self->align != 0) {
    size = size / self->align + 1;
    size *= self->align;
  }
  void *data = NULL;
  if (size > self->size) {
    data = neo_allocator_alloc(self->allocator, size, NULL);
    memset(data, 0, size);
    if (self->data) {
      memcpy(data, self->data, self->size);
      neo_allocator_free(self->allocator, self->data);
    }
    self->data = data;
    self->capacity = size;
  } else if (size < self->size) {
    if (size != 0) {
      data = neo_allocator_alloc(self->allocator, size, NULL);
      memset(data, 0, size);
    }
    if (self->data) {
      if (data) {
        memcpy(data, self->data, size);
      }
      neo_allocator_free(self->allocator, self->data);
    }
    self->data = data;
    self->size = size;
    self->capacity = size;
  }
}

void *neo_buffer_get(neo_buffer_t self) { return self->data; }
void neo_buffer_write(neo_buffer_t self, size_t offset, void *data,
                      size_t size) {
  if (size + offset > self->capacity) {
    neo_buffer_reserve(self, size + offset);
  }
  memcpy((uint8_t *)self->data + offset, data, size);
  self->size = offset + size;
}