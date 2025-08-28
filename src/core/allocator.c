#include "core/allocator.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct _neo_alloc_chunk {
  size_t size;
  neo_dispose_fn_t destructor_fn;
  const char *file;
  size_t line;
  struct _neo_alloc_chunk *next;
  struct _neo_alloc_chunk *last;
} *neo_alloc_chunk;

struct _neo_allocator_t {
  neo_alloc_fn_t alloc;
  neo_free_fn_t free;
  struct _neo_alloc_chunk begin;
  struct _neo_alloc_chunk end;
};

neo_allocator_t neo_create_allocator(neo_allocator_initialize_t *initialize) {
  neo_alloc_fn_t alloc_fn = NULL;
  if (initialize && initialize->alloc) {
    alloc_fn = initialize->alloc;
  } else {
    alloc_fn = malloc;
  }
  neo_free_fn_t free_fn = NULL;
  if (initialize && initialize->free) {
    free_fn = initialize->free;
  } else {
    free_fn = free;
  }
  neo_allocator_t allocator =
      (neo_allocator_t)alloc_fn(sizeof(struct _neo_allocator_t));
  if (!allocator) {
    return NULL;
  }
  allocator->alloc = alloc_fn;
  allocator->free = free_fn;
  allocator->begin.next = &allocator->end;
  allocator->begin.last = NULL;
  allocator->end.last = &allocator->begin;
  allocator->end.next = NULL;
  return allocator;
}

void neo_delete_allocator(neo_allocator_t allocator) {
  if (allocator) {
    neo_alloc_chunk chunk = allocator->begin.next;
    while (chunk != &allocator->end) {
      printf("memory leak: %zu bytes,allocated at %s:%zu\n", chunk->size,
             chunk->file, chunk->line);
      chunk = chunk->next;
    }
    allocator->free(allocator);
  }
}

void *neo_allocator_alloc_ex(neo_allocator_t self, size_t size,
                             neo_dispose_fn_t destructor_fn, const char *file,
                             size_t line) {
  neo_alloc_chunk chunk =
      (neo_alloc_chunk)self->alloc(sizeof(struct _neo_alloc_chunk) + size);
  if (!chunk) {
    return NULL;
  }
  chunk->size = size;
  chunk->file = file;
  chunk->line = line;
  chunk->destructor_fn = destructor_fn;
  chunk->last = self->end.last;
  chunk->next = &self->end;
  self->end.last->next = chunk;
  self->end.last = chunk;
  return &chunk[1];
}

void neo_allocator_free_ex(neo_allocator_t self, void *ptr) {
  if (!ptr) {
    return;
  }
  neo_alloc_chunk chunk =
      (neo_alloc_chunk)((uint8_t *)ptr - sizeof(struct _neo_alloc_chunk));
  if (chunk->destructor_fn) {
    chunk->destructor_fn(self, ptr);
  }
  chunk->last->next = chunk->next;
  chunk->next->last = chunk->last;
  self->free(chunk);
}