#ifndef _H_CORE_HASH_
#define _H_CORE_HASH_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t neo_hash_sdb(const char *str, uint32_t max_bucket);
uint32_t neo_hash_sdb_utf16(const uint16_t *str, uint32_t max_bucket);
#ifdef __cplusplus
};
#endif
#endif