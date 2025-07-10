#include "core/location.h"
#include "core/allocator.h"
#include "core/unicode.h"
#include <wchar.h>

bool neo_location_is(neo_location_t loc, const char *str) {
  const char *dst = loc.begin.offset;
  const char *src = str;
  while (*src) {
    if (*dst != *src) {
      return false;
    }
    dst++;
    src++;
  }
  if (src - str + loc.begin.offset == loc.end.offset) {
    return true;
  }
  return false;
}

wchar_t *neo_location_get(neo_allocator_t allocator, neo_location_t self) {
  size_t len = self.end.offset - self.begin.offset + 1;
  wchar_t *result = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
  wchar_t *dst = result;
  const char *src = self.begin.offset;
  while (src != self.end.offset) {
    neo_utf8_char chr = neo_utf8_read_char(src);
    src = chr.end;
    uint32_t utf32 = neo_utf8_char_to_utf32(chr);
    if (utf32 < 0xffff) {
      *dst++ = (uint16_t)utf32;
    } else if (utf32 < 0xeffff) {
      *dst++ = (uint16_t)(0xd800 + (utf32 >> 10) - 0x40);
      *dst++ = (uint16_t)(0xdc00 + (utf32 & 0x3ff));
    }
  }
  *dst = 0;
  return result;
}