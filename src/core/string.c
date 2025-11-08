#include "core/string.h"
#include "core/allocator.h"
#include "core/unicode.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
uint16_t *neo_string16_concat(neo_allocator_t allocator, uint16_t *src,
                              size_t *max, const uint16_t *str) {
  uint16_t *result = src;
  size_t len = neo_string16_length(str);
  uint16_t *dst = NULL;
  const uint16_t *psrc = NULL;
  size_t base = neo_string16_length(src);
  if (base + len + 1 > *max) {
    while (base + len + 1 > *max) {
      *max += NEO_STRING_CHUNK_SIZE;
    }
    result = neo_allocator_alloc(allocator, *max, NULL);
    psrc = src;
    dst = result;
    while (*psrc) {
      *dst++ = *psrc;
    }
    result[base] = 0;
    neo_allocator_free(allocator, src);
  }
  psrc = str;
  dst = result + base;
  while (*psrc) {
    *dst++ = *psrc;
  }
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
uint16_t *neo_create_string16(neo_allocator_t allocator, const uint16_t *src) {
  size_t len = neo_string16_length(src) + 1;
  uint16_t *str = neo_allocator_alloc(allocator, len * sizeof(uint16_t), NULL);
  memcpy(str, src, len * sizeof(uint16_t));
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
int64_t neo_string16_find(const uint16_t *source, const uint16_t *search) {
  size_t search_len = neo_string16_length(search);
  uint32_t next[search_len];
  {
    uint32_t j = 0;
    uint32_t k = -1;
    next[0] = -1;
    while (k < search_len - 1) {
      if (k == -1 || search[j] == search[k]) {
        k++;
        j++;
        next[j] = k;
      } else {
        k = next[k];
      }
    }
  }
  uint32_t i = 0;
  uint32_t j = 0;
  uint32_t length = neo_string16_length(source);
  while (i < length && j < search_len) {
    if (j == -1 || source[i] == search[j]) {
      i++;
      j++;
    } else {
      j = next[j];
    }
  }
  if (j == search_len) {
    return i - j;
  } else {
    return -1;
  }
}
uint16_t *neo_string_to_string16(neo_allocator_t allocator, const char *src) {
  uint16_t *str = neo_allocator_alloc(
      allocator, sizeof(uint16_t) * (strlen(src) + 1), NULL);
  uint16_t *dst = str;
  while (*src) {
    neo_utf8_char chr = neo_utf8_read_char(src);
    uint32_t utf32 = neo_utf8_char_to_utf32(chr);
    dst += neo_utf32_to_utf16(utf32, dst);
    src = chr.end;
  }
  *dst = 0;
  return str;
}
int neo_string16_compare(const uint16_t *str1, const uint16_t *str2) {
  for (;;) {
    if (*str1 != *str2) {
      return *str1 - *str2;
    }
    if (!*str1) {
      break;
    }
    str1++;
    str2++;
  }
  return 0;
}

int neo_string16_mix_compare(const uint16_t *str1, const char *str2) {
  for (;;) {
    if (*str1 != *str2) {
      return *str1 - *str2;
    }
    if (!*str1) {
      break;
    }
    str1++;
    str2++;
  }
  return 0;
}

char *neo_string16_to_string(neo_allocator_t allocator, const uint16_t *src) {
  size_t len = neo_string16_length(src);
  char *str = neo_allocator_alloc(allocator, len * 2 + 1, NULL);
  char *dst = str;
  while (*src) {
    neo_utf16_char chr = neo_utf16_read_char(src);
    src = chr.end;
    uint32_t utf32 = neo_utf16_to_utf32(chr);
    dst += neo_utf32_to_utf8(utf32, dst);
  }
  *dst = 0;
  return str;
}
double neo_string16_to_number(const uint16_t *str) {
  double value = 0;
  bool neg = false;
  while (IS_SPACE_SEPARATOR(*str) || *str == L'\u2028' || *str == L'\u2029' ||
         *str == '\n' || *str == '\r') {
    str++;
  }
  if (*str == '-') {
    neg = true;
    str++;
  } else {
    neg = false;
    str++;
  }
  if (*str == '0' && (str[1] == 'x' || str[1] == 'X')) {
    str += 2;
    while (*str) {
      if (*str >= '0' && *str <= '9') {
        value *= 16;
        value += *str - '0';
      } else if (*str >= 'a' && *str <= 'z') {
        value *= 16;
        value += *str - 'a';
      } else if (*str >= 'A' && *str <= 'Z') {
        value *= 16;
        value += *str - 'A';
      } else {
        break;
      }
      str++;
    }
  } else if (*str == '0' && (str[1] == 'o' || str[1] == 'O')) {
    str += 2;
    while (*str) {
      if (*str >= '0' && *str <= '7') {
        value *= 8;
        value += *str - '0';
      } else {
        break;
      }
      str++;
    }
  } else if (*str == '0' && (str[1] == 'b' || str[1] == 'B')) {
    str += 2;
    while (*str) {
      if (*str >= '0' && *str <= '1') {
        value *= 2;
        value += *str - '0';
      } else {
        break;
      }
      str++;
    }
  } else {
    while (*str) {
      if (*str >= '0' && *str <= '9') {
        value *= 10;
        value += *str - '0';
      } else {
        break;
      }
      str++;
    }
    if (*str == '.') {
      str++;
      double dec = 0;
      double offset = 1;
      while (*str) {
        if (*str >= '0' && *str <= '9') {
          dec += (*str - '0') / offset;
          offset /= 10;
        } else {
          break;
        }
        str++;
      }
      value += dec;
    }
    if (*str == 'e' || *str == 'E') {
      str++;
      bool pneg = false;
      if (*str == '+') {
        pneg = false;
        str++;
      } else if (*str == '-') {
        pneg = true;
        str++;
      }
      double p = 0;
      while (*str) {
        if (*str >= '0' && *str <= '9') {
          p *= 10;
          p += *str - '0';
        } else {
          break;
        }
        str++;
      }
      if (neg) {
        p = -p;
      }
      value *= log10(p);
    }
  }
  if (neg) {
    value = -value;
  }
  while (IS_SPACE_SEPARATOR(*str) || *str == L'\u2028' || *str == L'\u2029' ||
         *str == '\n' || *str == '\r') {
    str++;
  }
  if (*str) {
    value = NAN;
  }
  return value;
}