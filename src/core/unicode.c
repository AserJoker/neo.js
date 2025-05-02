#include "core/unicode.h"
#include "core/allocator.h"
noix_utf8_char noix_utf8_read_char(const char *str) {
  noix_utf8_char chr = {str, str};
  if ((*str & 0xe0) == 0xc0) {
    chr.end += 2;
  } else if ((*str & 0xf0) == 0xe0) {
    chr.end += 3;
  } else if ((*str & 0xf8) == 0xf0) {
    chr.end += 4;
  } else if ((*str & 0xfc) == 0xf8) {
    chr.end += 5;
  } else if ((*str & 0xfe) == 0xfc) {
    chr.end += 6;
  } else {
    chr.end += 1;
  }
  return chr;
}

char *noix_utf8_char_to_string(noix_allocator_t allocator, noix_utf8_char chr) {
  char *buf =
      (char *)noix_allocator_alloc(allocator, chr.end - chr.begin + 1, NULL);
  buf[chr.end - chr.begin] = 0;
  char *dst = buf;
  const char *src = chr.begin;
  while (src != chr.end) {
    *dst++ = *src++;
  }
  return buf;
}

size_t noix_utf8_get_len(const char *str) {
  const char *ptr = str;
  size_t len = 0;
  while (*ptr != 0) {
    noix_utf8_char chr = noix_utf8_read_char(ptr);
    ptr = chr.end;
    len++;
  }
  return len;
}