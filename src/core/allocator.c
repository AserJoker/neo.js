#include "core/allocator.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct _noix_alloc_chunk {
  size_t size;
  noix_destructor_fn_t destructor_fn;
  const char *file;
  size_t line;
  struct _noix_alloc_chunk *next;
  struct _noix_alloc_chunk *last;
} *noix_alloc_chunk;

struct _noix_allocator_t {
  noix_alloc_fn_t alloc;
  noix_free_fn_t free;
  struct _noix_alloc_chunk begin;
  struct _noix_alloc_chunk end;
};

noix_allocator_t
noix_create_allocator(noix_allocator_initialize_t *initialize) {
  noix_alloc_fn_t alloc_fn = NULL;
  if (initialize && initialize->alloc) {
    alloc_fn = initialize->alloc;
  } else {
    alloc_fn = malloc;
  }
  noix_free_fn_t free_fn = NULL;
  if (initialize && initialize->free) {
    free_fn = initialize->free;
  } else {
    free_fn = free;
  }
  noix_allocator_t allocator =
      (noix_allocator_t)alloc_fn(sizeof(struct _noix_allocator_t));
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

void noix_delete_allocator(noix_allocator_t allocator) {
  if (allocator) {
    noix_alloc_chunk chunk = allocator->begin.next;
    while (chunk != &allocator->end) {
      printf("memory leak: %zu bytes,allocated at %s:%zu", chunk->size,
             chunk->file, chunk->line);
      chunk = chunk->next;
    }
    allocator->free(allocator);
  }
}

void *noix_allocator_alloc(noix_allocator_t self, size_t size,
                           noix_destructor_fn_t destructor_fn, const char *file,
                           size_t line) {
  noix_alloc_chunk chunk =
      (noix_alloc_chunk)self->alloc(sizeof(struct _noix_alloc_chunk) + size);
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

void noix_allocator_free(noix_allocator_t self, void *ptr) {
  if (!ptr) {
    return;
  }
  noix_alloc_chunk chunk =
      (noix_alloc_chunk)((uint8_t *)ptr - sizeof(struct _noix_alloc_chunk));
  if (chunk->destructor_fn) {
    chunk->destructor_fn(self, ptr);
  }
  chunk->last->next = chunk->next;
  chunk->next->last = chunk->last;
  self->free(chunk);
}