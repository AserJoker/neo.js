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
wchar_t *neo_wstring_concat(neo_allocator_t allocator, wchar_t *src,
                            size_t *max, const wchar_t *str);

wchar_t *neo_wstring_encode_escape(neo_allocator_t allocator,
                                   const wchar_t *src);
char *neo_string_encode_escape(neo_allocator_t allocator, const char *src);

wchar_t *neo_wstring_decode_escape(neo_allocator_t allocator,
                                   const wchar_t *src);

char *neo_string_decode_escape(neo_allocator_t allocator, const char *src);

wchar_t *neo_wstring_decode(neo_allocator_t allocator, const wchar_t *src);

wchar_t *neo_create_wstring(neo_allocator_t allocator, const wchar_t *src);

char *neo_create_string(neo_allocator_t allocator, const char *src);

uint16_t *neo_wstring_to_char16(neo_allocator_t allocator, const wchar_t *src);

wchar_t *neo_wstring_to_lower(neo_allocator_t allocator, const wchar_t *src);
char *neo_string_to_lower(neo_allocator_t allocator, const char *src);

bool neo_wstring_end_with(const wchar_t *src, const wchar_t *text);
#endif