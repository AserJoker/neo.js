#include "core/string.h"
#include "core/allocator.h"
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
    result = neo_allocator_alloc(allocator, *max, NULL);
    wcscpy(result, src);
    result[base] = 0;
    neo_allocator_free(allocator, src);
  }
  wcscpy(result + base, str);
  result[base + len] = 0;
  return result;
}

char *neo_string_encode(neo_allocator_t allocator, const char *src) {
  size_t len = strlen(src);
  char *str = neo_allocator_alloc(allocator, len * 2 + 1, NULL);
  str[0] = 0;
  size_t idx = 0;
  size_t current = 0;
  for (; current < len; current++) {
    if (src[current] == '\n') {
      str[idx++] = '\\';
      str[idx++] = 'n';
    } else if (src[current] == '\r') {
      str[idx++] = '\\';
      str[idx++] = 'r';
    } else if (src[current] == '\"') {
      str[idx++] = '\\';
      str[idx++] = '"';
    } else {
      str[idx++] = src[current];
    }
  }
  str[idx] = 0;
  return str;
}
char *neo_create_string(neo_allocator_t allocator, const char *src) {
  char *str = neo_allocator_alloc(allocator, strlen(str) + 1, NULL);
  strcpy(str, src);
  return str;
}
wchar_t *neo_create_wstring(neo_allocator_t allocator, const wchar_t *src) {
  wchar_t *str =
      neo_allocator_alloc(allocator, (wcslen(src) + 1) * sizeof(wchar_t), NULL);
  wcscpy(str, src);
  str[wcslen(src)] = 0;
  return str;
}
