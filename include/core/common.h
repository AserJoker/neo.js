#ifndef _H_NOIX_COMMON_
#define _H_NOIX_COMMON_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*noix_alloc_fn_t)(size_t size);

typedef void (*noix_free_fn_t)(void *ptr);

typedef int8_t (*noix_compare_fn_t)(void *, void *);

#ifdef __cplusplus
};
#endif
#endif