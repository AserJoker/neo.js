#ifndef _H_CORE_STRING_
#define _H_CORE_STRING_
#include "core/allocator.h"
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

typedef uint16_t char16_t;
typedef uint8_t char8_t;
typedef uint32_t char32_t;
#define NEO_STRING_CHUNK_SIZE 16

char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str);

char *neo_string_encode_escape(neo_allocator_t allocator, const char *src);

char *neo_string_decode_escape(neo_allocator_t allocator, const char *src);

char *neo_string_decode(neo_allocator_t allocator, const char *src);

char *neo_create_string(neo_allocator_t allocator, const char *src);

char *neo_string_to_lower(neo_allocator_t allocator, const char *src);

size_t neo_string16_length(const uint16_t *str);

int64_t neo_string16_find(const uint16_t *source, const uint16_t *search);

uint16_t *neo_string_to_string16(neo_allocator_t allocator, const char *src);
#endif