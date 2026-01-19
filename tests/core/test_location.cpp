#include "neojs/core/location.h"
neo_location_t create_location(const char *src) {
  neo_location_t loc = {};
  loc.begin = {1, 1, src};
  loc.end = loc.begin;
  return loc;
}