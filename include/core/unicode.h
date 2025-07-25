#ifndef _H_NEO_CORE_UNICODE_
#define _H_NEO_CORE_UNICODE_
#ifdef __cplusplus
extern "C" {
#endif
#include "core/allocator.h"
#include "unicode.gen.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct _neo_utf8_char {
  const char *begin;
  const char *end;
} neo_utf8_char;

neo_utf8_char neo_utf8_read_char(const char *str);

uint32_t neo_utf8_char_to_utf32(neo_utf8_char chr);

char *neo_utf8_char_to_string(neo_allocator_t allocator, neo_utf8_char chr);

wchar_t *neo_utf8_char_to_wstring(neo_allocator_t allocator, neo_utf8_char chr);

char *neo_wstring_to_string(neo_allocator_t allocator, const wchar_t *wstring);

wchar_t *neo_string_to_wstring(neo_allocator_t allocator, const char *string);

size_t neo_utf8_get_len(const char *str);

bool neo_utf8_char_is(neo_utf8_char chr, const char *s);

bool neo_utf8_char_is_id_start(neo_utf8_char chr);

bool neo_utf8_char_is_id_continue(neo_utf8_char chr);

bool neo_utf8_char_is_space_separator(neo_utf8_char chr);

#ifdef __cplusplus
};
#endif
#endif