#include "core/unicode.h"
#include "core/allocator.h"
noix_utf8_char noix_utf8_read_char(const char *str) {
  noix_utf8_char chr = {str, str};
  if (*str == 0) {
    return chr;
  }
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

uint32_t noix_utf8_char_to_utf32(noix_utf8_char chr) {
  uint32_t value = 0;
  if (chr.end - chr.begin == 1) {
    value = *chr.begin;
  } else if (chr.end - chr.begin == 2) {
    value = *chr.begin & 0x1f;
    value <<= 6;
    value |= *(chr.begin + 1) & 0x3f;
  } else if (chr.end - chr.begin == 3) {
    value = *chr.begin & 0xf;
    value <<= 6;
    value |= *(chr.begin + 1) & 0x3f;
    value <<= 6;
    value |= *(chr.begin + 2) & 0x3f;
  } else if (chr.end - chr.begin == 4) {
    value = *chr.begin & 0x7;
    value <<= 6;
    value = *(chr.begin + 1) & 0xf;
    value <<= 6;
    value |= *(chr.begin + 2) & 0x3f;
    value <<= 6;
    value |= *(chr.begin + 3) & 0x3f;
  } else if (chr.end - chr.begin == 5) {
    value = *chr.begin & 0x2;
    value <<= 6;
    value = *(chr.begin + 1) & 0x3f;
    value <<= 6;
    value = *(chr.begin + 2) & 0x3f;
    value <<= 6;
    value |= *(chr.begin + 3) & 0x3f;
    value <<= 6;
    value |= *(chr.begin + 4) & 0x3f;
  } else if (chr.end - chr.begin == 6) {
    value = *chr.begin & 0x1;
    value <<= 6;
    value = *(chr.begin + 1) & 0x3f;
    value <<= 6;
    value = *(chr.begin + 2) & 0x3f;
    value <<= 6;
    value |= *(chr.begin + 3) & 0x3f;
    value <<= 6;
    value |= *(chr.begin + 4) & 0x3f;
  }
  return value;
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

bool noix_utf8_char_is(noix_utf8_char chr, const char *s) {
  const char *src = chr.begin;
  const char *dst = s;
  if (dst[chr.end - chr.begin] != 0) {
    return false;
  }
  while (src != chr.end) {
    if (*src != *dst) {
      return false;
    }
    src++;
    dst++;
  }
  return true;
}

bool noix_utf8_char_is_id_start(noix_utf8_char chr) {
  if (chr.end - chr.begin == 0) {
    return false;
  }
  uint32_t utf32 = noix_utf8_char_to_utf32(chr);
  return IS_ID_START(utf32);
}

bool noix_utf8_char_is_id_continue(noix_utf8_char chr) {
  if (chr.end - chr.begin == 0) {
    return false;
  }
  uint32_t utf32 = noix_utf8_char_to_utf32(chr);
  return IS_ID_CONTINUE(utf32);
}