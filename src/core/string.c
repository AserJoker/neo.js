#include "core/string.h"
#include "core/allocator.h"
#include <string.h>

char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str) {
  char *result = src;
  size_t len = strlen(str);
  size_t base = strlen(src);
  if (base + len > *max) {
    while (base + len > *max) {
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