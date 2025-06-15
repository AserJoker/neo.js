#ifndef _H_CORE_STRING_
#define _H_CORE_STRING_
#include "core/allocator.h"
char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str);
char *neo_string_encode(neo_allocator_t allocator, const char *src);
char *neo_create_string(neo_allocator_t allocator, const char *src);
wchar_t *neo_create_wstring(neo_allocator_t allocator, const wchar_t *src);
#endif