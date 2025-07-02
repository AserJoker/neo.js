#ifndef _H_NEO_CORE_CLOCK_
#define _H_NEO_CORE_CLOCK_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

uint64_t neo_clock_get_timestamp();

void neo_clock_sleep(uint64_t timeout);

#ifdef __cplusplus
};
#endif
#endif