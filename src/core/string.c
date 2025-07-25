#include "core/string.h"
#include "core/allocator.h"
#include <stdint.h>
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

wchar_t *neo_wstring_encode(neo_allocator_t allocator, const wchar_t *src) {
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

wchar_t *neo_wstring_decode(neo_allocator_t allocator, const wchar_t *src) {
  size_t len = wcslen(src);
  wchar_t *str =
      neo_allocator_alloc(allocator, (len * 2 + 1) * sizeof(wchar_t), NULL);
  size_t idx = 0;
  size_t current = 0;
  for (; current < len; current++) {
    if (src[current] == '\\') {
      current++;
      if (current == 'n') {
        str[idx++] = '\n';
      } else if (current == 'r') {
        str[idx++] = '\r';
      } else if (current == '\\') {
        str[idx++] = '\\';
      } else if (current == '\"') {
        str[idx++] = '\"';
      } else {
        str[idx++] = src[current];
      }
      continue;
    } else {
      str[idx++] = src[current];
    }
  }
  str[idx] = 0;
  return str;
}
wchar_t *neo_create_wstring(neo_allocator_t allocator, const wchar_t *src) {
  size_t len = wcslen(src) + 1;
  wchar_t *str = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
  wcscpy(str, src);
  str[wcslen(src)] = 0;
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