#include "core/string.h"
#include "core/allocator.h"
#include <string.h>
struct _neo_string_t {
  neo_allocator_t allocator;
  char *data;
  size_t size;
};

static void neo_string_dispose(neo_allocator_t allocator, neo_string_t self) {
  neo_allocator_free(allocator, self->data);
  self->size = 0;
}

neo_string_t neo_create_string(neo_allocator_t allocator, const char *source) {
  size_t len = source != NULL ? strlen(source) : 1;
  neo_string_t string = neo_allocator_alloc(
      allocator, sizeof(struct _neo_string_t), neo_string_dispose);
  string->size = len;
  string->data = neo_allocator_alloc(allocator, len + 1, NULL);
  if (source != NULL) {
    strcpy(string->data, source);
  }
  string->data[len] = 0;
  string->allocator = allocator;
  return string;
}

neo_string_t neo_clone_string(neo_allocator_t allocator, neo_string_t source) {
  return neo_create_string(allocator, source->data);
}

size_t neo_string_get_length(neo_string_t self) { return self->size; }

const char *neo_string_get(neo_string_t self) { return self->data; }

neo_string_t neo_string_concat(neo_string_t self, const char *another) {
  size_t size = self->size + strlen(another);
  char *buf = neo_allocator_alloc(self->allocator, size + 1, NULL);
  strcpy(buf, self->data);
  strcpy(buf + self->size, another);
  buf[size] = 0;
  if (self->data) {
    neo_allocator_free(self->allocator, self->data);
  }
  self->data = buf;
  self->size = size;
  return self;
}