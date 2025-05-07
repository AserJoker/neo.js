#include "core/location.h"

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
  return true;
}