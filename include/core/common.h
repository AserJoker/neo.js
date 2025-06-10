#ifndef _H_NEO_COMMON_
#define _H_NEO_COMMON_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*neo_alloc_fn_t)(size_t size);

typedef void (*neo_free_fn_t)(void *ptr);

typedef int8_t (*neo_compare_fn_t)(const void *, const void *);

#ifdef __cplusplus
};
#endif
#endif