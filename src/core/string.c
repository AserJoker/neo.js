#include "core/string.h"
#include "core/allocator.h"
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
      *max += 128;
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

wchar_t *neo_wstring_concat(neo_allocator_t allocator, wchar_t *src,
                            size_t *max, const wchar_t *str) {
  wchar_t *result = src;
  size_t len = wcslen(str);
  size_t base = wcslen(src);
  if (base + len + 1 > *max) {
    while (base + len + 1 > *max) {
      *max += 128;
    }
    result = neo_allocator_alloc(allocator, *max * sizeof(wchar_t), NULL);
    wcscpy(result, src);
    result[base] = 0;
    neo_allocator_free(allocator, src);
  }
  wcscpy(result + base, str);
  result[base + len] = 0;
  return result;
}

wchar_t *neo_wstring_encode_escape(neo_allocator_t allocator,
                                   const wchar_t *src) {
  size_t len = wcslen(src);
  wchar_t *str =
      neo_allocator_alloc(allocator, (len * 2 + 1) * sizeof(wchar_t), NULL);
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
    } else if (src[current] == '\"') {
      str[idx++] = '\\';
      str[idx++] = '"';
    } else if (src[current] == '\t') {
      str[idx++] = '\\';
      str[idx++] = 't';
    } else if (src[current] == '\'') {
      str[idx++] = '\\';
      str[idx++] = '\'';
    } else {
      str[idx++] = src[current];
    }
  }
  str[idx] = 0;
  return str;
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
    } else if (src[current] == '\"') {
      str[idx++] = '\\';
      str[idx++] = '"';
    } else if (src[current] == '\t') {
      str[idx++] = '\\';
      str[idx++] = 't';
    } else if (src[current] == '\'') {
      str[idx++] = '\\';
      str[idx++] = '\'';
    } else {
      str[idx++] = src[current];
    }
  }
  str[idx] = 0;
  return str;
}

wchar_t *neo_wstring_decode_escape(neo_allocator_t allocator,
                                   const wchar_t *src) {
  size_t len = wcslen(src);
  wchar_t *str =
      neo_allocator_alloc(allocator, (len + 1) * sizeof(wchar_t), NULL);
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
      } else if (src[current] == '\\') {
        str[idx++] = '\\';
      } else if (src[current] == '\"') {
        str[idx++] = '\"';
      } else if (src[current] == '\'') {
        str[idx++] = '\'';
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
      } else if (src[current] == '\\') {
        str[idx++] = '\\';
      } else if (src[current] == '\"') {
        str[idx++] = '\"';
      } else if (src[current] == '\'') {
        str[idx++] = '\'';
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

static uint8_t neo_char_to_int(wchar_t chr) {
  if (chr >= '0' && chr <= '9') {
    return chr - '0';
  } else if (chr >= 'a' && chr <= 'f') {
    return chr - 'a' + 10;
  } else if (chr >= 'A' && chr <= 'F') {
    return chr - 'A' + 10;
  }
  return (uint8_t)-1;
}

wchar_t *neo_wstring_decode(neo_allocator_t allocator, const wchar_t *src) {
  size_t len = wcslen(src) + 1;
  wchar_t *str = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
  wchar_t *dst = str;
  while (*src) {
    if (*src == '\\') {
      wchar_t code = 0;
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
        if (utf32 < 0xffff) {
          *dst++ = (uint16_t)utf32;
        } else if (utf32 < 0xeffff) {
          *dst++ = (uint16_t)(0xd800 + (utf32 >> 10) - 0x40);
          *dst++ = (uint16_t)(0xdc00 + (utf32 & 0x3ff));
        }
      } else if (*src == 'n') {
        src++;
        *dst++ = '\n';
      } else if (*src == 'r') {
        src++;
        *dst++ = '\r';
      } else if (*src == 't') {
        src++;
        *dst++ = '\t';
      } else if (*src == '\\') {
        src++;
        *dst++ = '\\';
      } else if (*src == '\"') {
        src++;
        *dst++ = '\"';
      } else if (*src == '\'') {
        src++;
        *dst++ = '\'';
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

wchar_t *neo_create_wstring(neo_allocator_t allocator, const wchar_t *src) {
  size_t len = wcslen(src) + 1;
  wchar_t *str = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
  wcscpy(str, src);
  str[wcslen(src)] = 0;
  return str;
}

char *neo_create_string(neo_allocator_t allocator, const char *src) {
  size_t len = strlen(src) + 1;
  char *str = neo_allocator_alloc(allocator, len * sizeof(char), NULL);
  strcpy(str, src);
  str[strlen(src)] = 0;
  return str;
}

uint16_t *neo_wstring_to_char16(neo_allocator_t allocator, const wchar_t *src) {
  if (sizeof(wchar_t) != sizeof(uint16_t)) {
    size_t len = wcslen(src) + 1;
    uint16_t *pstr =
        neo_allocator_alloc(allocator, len * sizeof(uint16_t), NULL);
    size_t idx = 0;
    for (idx = 0; src[idx] != 0; idx++) {
      pstr[idx] = src[idx];
    }
    pstr[idx] = 0;
    return pstr;
  } else {
    return (uint16_t *)neo_create_wstring(allocator, src);
  }
}
wchar_t *neo_wstring_to_lower(neo_allocator_t allocator, const wchar_t *src) {
  size_t len = wcslen(src) + 1;
  wchar_t *str = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
  for (size_t idx = 0; idx < len; idx++) {
    if (src[idx] >= 'A' && src[idx] <= 'Z') {
      str[idx] = src[idx] - 'A';
    } else {
      str[idx] = src[idx];
    }
  }
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

bool neo_wstring_end_with(const wchar_t *src, const wchar_t *text) {
  size_t len = wcslen(src);
  if (wcslen(text) > len) {
    return false;
  }
  len -= wcslen(text);
  src += len;
  while (*src) {
    if (*src != *text) {
      break;
    }
    src++;
    text++;
  }
  return *src == 0;
}