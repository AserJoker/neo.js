#ifndef _H_NEO_CORE_BIGINT_
#define _H_NEO_CORE_BIGINT_
#include "core/allocator.h"
#include <stdbool.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef chunk_t
#define chunk_t uint8_t
#endif
typedef struct _neo_bigint_t *neo_bigint_t;

neo_bigint_t neo_create_bigint(neo_allocator_t allocaotr);

neo_bigint_t neo_number_to_bigint(neo_allocator_t allocator, int64_t val);

neo_bigint_t neo_string_to_bigint(neo_allocator_t allocator,
                                  const wchar_t *val);

neo_bigint_t neo_bigint_clone(neo_bigint_t self);

wchar_t *neo_bigint_to_string(neo_bigint_t bigint);

double neo_bigint_to_number(neo_bigint_t bigint);

bool neo_bigint_is_negative(neo_bigint_t bigint);

neo_bigint_t neo_bigint_abs(neo_bigint_t self);

neo_bigint_t neo_bigint_add(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_sub(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_mul(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_div(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_mod(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_pow(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_and(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_or(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_xor(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_shr(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_shl(neo_bigint_t self, neo_bigint_t another);

neo_bigint_t neo_bigint_not(neo_bigint_t self);

bool neo_bigint_is_equal(neo_bigint_t self, neo_bigint_t another);

bool neo_bigint_is_greater(neo_bigint_t self, neo_bigint_t another);

bool neo_bigint_is_less(neo_bigint_t self, neo_bigint_t another);

bool neo_bigint_is_greater_or_equal(neo_bigint_t self, neo_bigint_t another);

bool neo_bigint_is_less_or_equal(neo_bigint_t self, neo_bigint_t another);

#ifdef __cplusplus
};
#endif
#endif