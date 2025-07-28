#ifndef _H_NEO_COMMON_
#define _H_NEO_COMMON_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*neo_alloc_fn_t)(size_t size);

typedef void (*neo_free_fn_t)(void *ptr);

typedef int32_t (*neo_compare_fn_t)(const void *, const void *, void *);

typedef uint32_t (*neo_hash_fn_t)(const void *, uint32_t, void *);

#ifdef __cplusplus
};
#endif
#endif