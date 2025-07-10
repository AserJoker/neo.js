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
    if (*src == '\\' && *(src + 1) == 'x') {
      char *buf = neo_allocator_alloc(allocator, len, NULL);
      size_t offset = 0;
      while (*src == '\\' && *(src + 1) == 'x') {
        src += 2;
        char chr = 0;
        for (int idx = 0; idx < 2; idx++) {
          chr *= 16;
          if (*src >= '0' && *src <= '9') {
            chr += *src - '0';
          } else if (*src >= 'a' && *src <= 'f') {
            chr += *src - 'a' + 10;
          } else if (*src >= 'A' && *src <= 'F') {
            chr += *src - 'A' + 10;
          }
          src++;
        }
        buf[offset++] = chr;
      }
      buf[offset] = 0;
      const char *ptr = buf;
      while (*ptr) {
        neo_utf8_char chr = neo_utf8_read_char(ptr);
        ptr = chr.end;
        uint32_t utf32 = neo_utf8_char_to_utf32(chr);
        if (utf32 < 0xffff) {
          *dst++ = (uint16_t)utf32;
        } else if (utf32 < 0xeffff) {
          *dst++ = (uint16_t)(0xd800 + (utf32 >> 10) - 0x40);
          *dst++ = (uint16_t)(0xdc00 + (utf32 & 0x3ff));
        }
      }
      neo_allocator_free(allocator, buf);
    } else {
      uint32_t utf32 = 0;
      if (*src == '\\' && *(src + 1) == 'u') {
        src += 2;
        if (*src == '{') {
          src++;
          while (*src != '}') {
            utf32 *= 16;
            if (*src >= '0' && *src <= '9') {
              utf32 += *src - '0';
            }
            if (*src >= 'a' && *src <= 'f') {
              utf32 += *src - 'a' + 10;
            }
            if (*src >= 'A' && *src <= 'F') {
              utf32 += *src - 'A' + 10;
            }
            src++;
          }
          src++;
        } else {
          for (int8_t idx = 0; idx < 4; idx++) {
            utf32 *= 16;
            if (*src >= '0' && *src <= '9') {
              utf32 += *src - '0';
            }
            if (*src >= 'a' && *src <= 'f') {
              utf32 += *src - 'a' + 10;
            }
            if (*src >= 'A' && *src <= 'F') {
              utf32 += *src - 'A' + 10;
            }
            src++;
          }
        }
      } else {
        neo_utf8_char chr = neo_utf8_read_char(src);
        src = chr.end;
        utf32 = neo_utf8_char_to_utf32(chr);
      }
      if (utf32 < 0xffff) {
        *dst++ = (uint16_t)utf32;
      } else if (utf32 < 0xeffff) {
        *dst++ = (uint16_t)(0xd800 + (utf32 >> 10) - 0x40);
        *dst++ = (uint16_t)(0xdc00 + (utf32 & 0x3ff));
      }
    }
  }
  *dst = 0;
  return result;
}