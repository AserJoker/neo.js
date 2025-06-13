#include "core/hash.h"
uint32_t neo_hash_sdb(const wchar_t *str, uint32_t max_bucket) {
  uint32_t hash = 0;
  while (*str) {
    hash = (*str++) + (hash << 6) + (hash << 16) - hash;
  }
  return (hash & 0x7FFFFFFF) % max_bucket;
}