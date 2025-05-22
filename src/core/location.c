#include "core/location.h"
#include "core/allocator.h"

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

char *neo_location_get(neo_allocator_t allocator, neo_location_t self) {
  size_t len = self.end.offset - self.begin.offset;
  char *buf = neo_allocator_alloc(allocator, len + 1, NULL);
  char *dst = buf;
  const char *src = self.begin.offset;
  while (src != self.end.offset) {
    *dst++ = *src++;
  }
  *dst = 0;
  return buf;
}