#include "neojs/core/unicode.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
neo_utf8_char neo_utf8_read_char(const char *str) {
  neo_utf8_char chr = {str, str};
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

neo_utf16_char neo_utf16_read_char(const uint16_t *str) {
  neo_utf16_char chr;
  chr.begin = str;
  chr.end = str;
  if (*chr.end >= 0xd800 && *chr.end <= 0xdfff) {
    chr.end += 2;
  } else {
    chr.end++;
  }
  return chr;
}

uint32_t neo_utf8_char_to_utf32(neo_utf8_char chr) {
  uint32_t value = 0;
  const char *s = chr.begin;
  if ((*s & 0b10000000) == 0b00000000) {
    value = *s;
  } else if ((*s & 0b11100000) == 0b11000000) {
    value = ((s[0] & 0b00011111) << 6) | (s[1] & 0b00111111);
  } else if ((*s & 0b11110000) == 0b11100000) {
    value = ((s[0] & 0b00001111) << 12) | ((s[1] & 0b00111111) << 6) |
            (s[2] & 0b00111111);
  } else if ((*s & 0b11111000) == 0b11110000) {
    value = ((s[0] & 0b00000111) << 18) | ((s[1] & 0b00111111) << 12) |
            ((s[2] & 0b00111111) << 6) | (s[3] & 0b00111111);
  }
  return value;
}

uint32_t neo_utf16_to_utf32(neo_utf16_char chr) {
  if (chr.end - chr.begin == 1) {
    return *chr.begin;
  } else {
    return 0x10000 + (((((uint32_t)*chr.begin) - 0xD800) << 10) |
                      (((uint32_t)*(chr.begin + 1)) - 0xDC00));
  }
}

size_t neo_utf32_to_utf8(uint32_t utf32, char *output) {
  char *s = output;
  s[0] = 0;
  if (utf32 < 0x7f) {
    s[0] = (uint8_t)utf32;
    return 1;
  } else if (utf32 < 0x7ff) {
    s[0] = (utf32 >> 6) | 0xC0;
    s[1] = (utf32 & 0x3F) | 0x80;
    return 2;
  } else if (utf32 < 0xFFFF) {
    s[0] = (utf32 >> 12) | 0xE0;
    s[1] = ((utf32 >> 6) & 0x3F) | 0x80;
    s[2] = (utf32 & 0x3F) | 0x80;
    return 3;
  } else if (utf32 < 0x10FFFF) {
    s[0] = (utf32 >> 18) | 0xF0;
    s[1] = ((utf32 >> 12) & 0x3F) | 0x80;
    s[2] = ((utf32 >> 6) & 0x3F) | 0x80;
    s[3] = (utf32 & 0x3F) | 0x80;
    return 4;
  }
  return 0;
}
size_t neo_utf32_to_utf16(uint32_t utf32, uint16_t *output) {
  if (utf32 < 0xffff) {
    if (output) {
      *output = (uint16_t)utf32;
    }
    return 1;
  } else {
    uint32_t offset = utf32 - 0x10000;
    if (output) {
      *output = (uint16_t)((offset >> 10) + 0xD800);
      *(output + 1) = (uint16_t)((offset & 0x3FF) + 0xDC00);
    }
    return 2;
  }
}

bool neo_utf8_char_is(neo_utf8_char chr, const char *s) {
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

bool neo_utf8_char_is_id_start(neo_utf8_char chr) {
  if (chr.end - chr.begin == 0) {
    return false;
  }
  uint32_t utf32 = neo_utf8_char_to_utf32(chr);
  return IS_ID_START(utf32);
}

bool neo_utf8_char_is_id_continue(neo_utf8_char chr) {
  if (chr.end - chr.begin == 0) {
    return false;
  }
  uint32_t utf32 = neo_utf8_char_to_utf32(chr);
  return IS_ID_CONTINUE(utf32);
}
bool neo_utf8_char_is_space_separator(neo_utf8_char chr) {
  if (chr.end - chr.begin == 0) {
    return false;
  }
  uint32_t utf32 = neo_utf8_char_to_utf32(chr);
  return IS_SPACE_SEPARATOR(utf32);
}
