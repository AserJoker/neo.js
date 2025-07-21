#ifndef _H_NEO_CORE_CLOCK_
#define _H_NEO_CORE_CLOCK_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int32_t neo_clock_get_timezone();

int64_t neo_clock_get_utc_timestamp();

void neo_clock_sleep(uint64_t timeout);

#ifdef __cplusplus
};
#endif
#endif