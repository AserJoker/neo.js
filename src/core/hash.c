#include "neojs/core/hash.h"
uint32_t neo_hash_sdb(const char *str) {
  uint32_t hash = 0;
  while (*str) {
    hash = (*str++) + (hash << 6) + (hash << 16) - hash;
  }
  return (hash & 0x7FFFFFFF);
}
uint32_t neo_hash_sdb_utf16(const uint16_t *str) {
  uint32_t hash = 0;
  while (*str) {
    hash = (*str++) + (hash << 6) + (hash << 16) - hash;
  }
  return (hash & 0x7FFFFFFF);
}