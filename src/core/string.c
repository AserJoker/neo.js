#include "core/string.h"
#include "core/allocator.h"
#include "core/unicode.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str) {
  char *result = src;
  size_t len = strlen(str);
  size_t base = strlen(src);
  if (base + len + 1 > *max) {
    while (base + len + 1 > *max) {
      *max += NEO_STRING_CHUNK_SIZE;
    }
    result = neo_allocator_alloc(allocator, *max, NULL);
    strcpy(result, src);
    result[base] = 0;
    neo_allocator_free(allocator, src);
  }
  strcpy(result + base, str);
  result[base + len] = 0;
  return result;
}

char *neo_string_encode_escape(neo_allocator_t allocator, const char *src) {
  size_t len = strlen(src);
  char *str =
      neo_allocator_alloc(allocator, (len * 2 + 1) * sizeof(char), NULL);
  size_t idx = 0;
  size_t current = 0;
  for (; current < len; current++) {
    if (src[current] == '\\') {
      str[idx++] = '\\';
      str[idx++] = '\\';
    } else if (src[current] == '\n') {
      str[idx++] = '\\';
      str[idx++] = 'n';
    } else if (src[current] == '\r') {
      str[idx++] = '\\';
      str[idx++] = 'r';
    } else if (src[current] == '\t') {
      str[idx++] = '\\';
      str[idx++] = 't';
    } else if (src[current] == '\b') {
      str[idx++] = '\\';
      str[idx++] = 'b';
    } else if (src[current] == '\v') {
      str[idx++] = '\\';
      str[idx++] = 'v';
    } else if (src[current] == '\f') {
      str[idx++] = '\\';
      str[idx++] = 'f';
    } else {
      str[idx++] = src[current];
    }
  }
  str[idx] = 0;
  return str;
}

char *neo_string_decode_escape(neo_allocator_t allocator, const char *src) {
  size_t len = strlen(src);
  char *str = neo_allocator_alloc(allocator, (len + 1) * sizeof(char), NULL);
  size_t idx = 0;
  size_t current = 0;
  for (; current < len; current++) {
    if (src[current] == '\\') {
      current++;
      if (src[current] == 'n') {
        str[idx++] = '\n';
      } else if (src[current] == 'r') {
        str[idx++] = '\r';
      } else if (src[current] == 't') {
        str[idx++] = '\t';
      } else if (src[current] == 'b') {
        str[idx++] = '\b';
      } else if (src[current] == 'f') {
        str[idx++] = '\f';
      } else if (src[current] == 'v') {
        str[idx++] = '\v';
      } else {
        str[idx++] = src[current];
      }
    } else {
      str[idx++] = src[current];
    }
  }
  str[idx] = 0;
  return str;
}

static uint8_t neo_char_to_int(char chr) {
  if (chr >= '0' && chr <= '9') {
    return chr - '0';
  } else if (chr >= 'a' && chr <= 'f') {
    return chr - 'a' + 10;
  } else if (chr >= 'A' && chr <= 'F') {
    return chr - 'A' + 10;
  }
  return (uint8_t)-1;
}

char *neo_string_decode(neo_allocator_t allocator, const char *src) {
  size_t len = strlen(src) + 1;
  char *str = neo_allocator_alloc(allocator, len * sizeof(char), NULL);
  char *dst = str;
  while (*src) {
    if (*src == '\\') {
      char code = 0;
      src++;
      if (*src == 'x') {
        for (int8_t idx = 0; idx < 2; idx++) {
          code *= 16;
          uint8_t ch = neo_char_to_int(*src);
          if (ch == (uint8_t)-1) {
            goto onerror;
          }
          code += ch;
          src++;
        }
        *dst++ = code;
      } else if (*src == 'u') {
        src++;
        uint32_t utf32 = 0;
        if (*src == '{') {
          src++;
          while (*src && *src != '}') {
            utf32 *= 16;
            uint8_t ch = neo_char_to_int(*src);
            if (ch == (uint8_t)-1) {
              goto onerror;
            }
            utf32 += ch;
            src++;
          }
          if (!*src) {
            goto onerror;
          }
          src++;
        } else {
          for (int8_t idx = 0; idx < 4; idx++) {
            utf32 *= 16;
            uint8_t ch = neo_char_to_int(*src);
            if (ch == (uint8_t)-1) {
              goto onerror;
            }
            utf32 += ch;
            src++;
          }
        }
        dst += neo_utf32_to_utf8(utf32, dst);
      } else if (*src == 'n') {
        src++;
        *dst++ = '\n';
      } else if (*src == 'r') {
        src++;
        *dst++ = '\r';
      } else if (*src == 't') {
        src++;
        *dst++ = '\t';
      } else if (*src == 'b') {
        src++;
        *dst++ = '\b';
      } else if (*src == 'f') {
        src++;
        *dst++ = '\f';
      } else if (*src == 'v') {
        src++;
        *dst++ = '\v';
      } else {
        *dst++ = *src++;
      }
    } else {
      *dst++ = *src++;
    }
  }
  *dst = 0;
  return str;
onerror:
  neo_allocator_free(allocator, str);
  return NULL;
}

char *neo_create_string(neo_allocator_t allocator, const char *src) {
  size_t len = strlen(src) + 1;
  char *str = neo_allocator_alloc(allocator, len * sizeof(char), NULL);
  strcpy(str, src);
  str[strlen(src)] = 0;
  return str;
}
char *neo_string_to_lower(neo_allocator_t allocator, const char *src) {
  size_t len = strlen(src) + 1;
  char *str = neo_allocator_alloc(allocator, sizeof(char) * len, NULL);
  for (size_t idx = 0; idx < len; idx++) {
    if (src[idx] >= 'A' && src[idx] <= 'Z') {
      str[idx] = src[idx] - 'A';
    } else {
      str[idx] = src[idx];
    }
  }
  return str;
}
size_t neo_string16_length(const uint16_t *str) {
  size_t len = 0;
  while (*str) {
    str++;
    len++;
  }
  return len;
}