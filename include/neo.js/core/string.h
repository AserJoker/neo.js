#ifndef _H_CORE_STRING_
#define _H_CORE_STRING_
#include "neo.js/core/allocator.h"
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NEO_STRING_CHUNK_SIZE 16
char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str);
char *neo_string_encode_escape(neo_allocator_t allocator, const char *src);
char *neo_string_decode_escape(neo_allocator_t allocator, const char *src);
char *neo_string_decode(neo_allocator_t allocator, const char *src);
char *neo_create_string(neo_allocator_t allocator, const char *src);
char *neo_string_to_lower(neo_allocator_t allocator, const char *src);
uint16_t *neo_string16_concat(neo_allocator_t allocator, uint16_t *src,
                              size_t *max, const uint16_t *str);
uint16_t *neo_create_string16(neo_allocator_t allocator, const uint16_t *src);
size_t neo_string16_length(const uint16_t *str);
int64_t neo_string16_find(const uint16_t *source, const uint16_t *search);
uint16_t *neo_string_to_string16(neo_allocator_t allocator, const char *src);
int neo_string16_compare(const uint16_t *str1, const uint16_t *str2);
int neo_string16_mix_compare(const uint16_t *str1, const char *str2);
char *neo_string16_to_string(neo_allocator_t allocator, const uint16_t *src);
double neo_string16_to_number(const uint16_t *str);
uint16_t *neo_string16_decode(neo_allocator_t allocator, const uint16_t *src);
#ifdef __cplusplus
}
#endif
#endif