#ifndef _H_CORE_STRING_
#define _H_CORE_STRING_
#include "core/allocator.h"
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

typedef uint16_t char16;
typedef uint8_t char8;
typedef uint32_t char32;

char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str);

char *neo_string_encode_escape(neo_allocator_t allocator, const char *src);

char *neo_string_decode_escape(neo_allocator_t allocator, const char *src);

char *neo_string_decode(neo_allocator_t allocator, const char *src);

char *neo_create_string(neo_allocator_t allocator, const char *src);

char *neo_string_to_lower(neo_allocator_t allocator, const char *src);
#endif