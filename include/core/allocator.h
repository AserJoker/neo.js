#ifndef _H_NEO_CORE_ALLOCATOR_
#define _H_NEO_CORE_ALLOCATOR_
#ifdef __cplusplus
extern "C" {
#endif
#include "common.h"
#include <stddef.h>
typedef struct _neo_allocator_t *neo_allocator_t;

typedef void (*neo_dispose_fn_t)(neo_allocator_t allocator, void *pobject);
typedef struct _neo_allocator_initialize_t {
  neo_alloc_fn_t alloc;
  neo_free_fn_t free;
} neo_allocator_initialize_t;

neo_allocator_t neo_create_allocator(neo_allocator_initialize_t *initialize);

void neo_delete_allocator(neo_allocator_t allocator);

static inline neo_allocator_t neo_create_default_allocator() {
  return neo_create_allocator(NULL);
}

void *neo_allocator_alloc_ex(neo_allocator_t self, size_t size,
                             neo_dispose_fn_t destructor_fn, const char *file,
                             size_t line);

#define neo_allocator_alloc(self, size, destructor)                            \
  neo_allocator_alloc_ex(self, size, (neo_dispose_fn_t)destructor, __FILE__,   \
                         __LINE__)

void neo_allocator_free_ex(neo_allocator_t self, void *ptr);

#define neo_allocator_free(self, ptr)                                          \
  do {                                                                         \
    neo_allocator_free_ex(self, ptr);                                          \
    ptr = NULL;                                                                \
  } while (0)
#ifdef __cplusplus
};
#endif
#endif