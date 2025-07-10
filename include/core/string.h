#ifndef _H_CORE_STRING_
#define _H_CORE_STRING_
#include "core/allocator.h"
char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str);
wchar_t *neo_wstring_concat(neo_allocator_t allocator, wchar_t *src,
                            size_t *max, const wchar_t *str);
char *neo_string_encode(neo_allocator_t allocator, const char *src);
wchar_t *neo_wstring_encode(neo_allocator_t allocator, const wchar_t *src);
char *neo_create_string(neo_allocator_t allocator, const char *src);
wchar_t *neo_create_wstring(neo_allocator_t allocator, const wchar_t *src);
#endif