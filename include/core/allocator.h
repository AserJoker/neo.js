#ifndef _H_NOIX_CORE_ALLOCATOR_
#define _H_NOIX_CORE_ALLOCATOR_
#include <stddef.h>
typedef struct _noix_allocator_t *noix_allocator_t;

typedef void *(*noix_alloc_fn_t)(size_t size);

typedef void (*noix_free_fn_t)(void *ptr);

typedef void (*noix_destructor_fn_t)(noix_allocator_t allocator, void *pobject);

typedef struct _noix_allocator_initialize_t {
  noix_alloc_fn_t alloc;
  noix_free_fn_t free;
} noix_allocator_initialize_t;

noix_allocator_t noix_create_allocator(noix_allocator_initialize_t *initialize);

void noix_delete_allocator(noix_allocator_t allocator);

static inline noix_allocator_t noix_create_default_allocator() {
  return noix_create_allocator(NULL);
}

void *noix_allocator_alloc_ex(noix_allocator_t self, size_t size,
                              noix_destructor_fn_t destructor_fn,
                              const char *file, size_t line);

#define noix_allocator_alloc(self, size, destructor)                           \
  noix_allocator_alloc_ex(self, size, (noix_destructor_fn_t)destructor,        \
                          __FILE__, __LINE__)

void noix_allocator_free(noix_allocator_t self, void *ptr);

#endif