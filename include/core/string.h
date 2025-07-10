#ifndef _H_CORE_STRING_
#define _H_CORE_STRING_
#include "core/allocator.h"
#include <stdbool.h>
#include <wchar.h>
char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str);
wchar_t *neo_wstring_concat(neo_allocator_t allocator, wchar_t *src,
                            size_t *max, const wchar_t *str);

wchar_t *neo_wstring_encode(neo_allocator_t allocator, const wchar_t *src);

wchar_t *neo_create_wstring(neo_allocator_t allocator, const wchar_t *src);

bool neo_wstring_end_with(const wchar_t *src, const wchar_t *text);
#endif