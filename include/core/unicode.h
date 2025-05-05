#ifndef _H_NOIX_CORE_UNICODE_
#define _H_NOIX_CORE_UNICODE_
#ifdef __cplusplus
extern "C" {
#endif
#include "core/allocator.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct _noix_utf8_char {
  const char *begin;
  const char *end;
} noix_utf8_char;

noix_utf8_char noix_utf8_read_char(const char *str);

char *noix_utf8_char_to_string(noix_allocator_t allocator, noix_utf8_char chr);

size_t noix_utf8_get_len(const char *str);

bool noix_utf8_char_is(noix_utf8_char chr, const char *s);

#ifdef __cplusplus
};
#endif
#endif