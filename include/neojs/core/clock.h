#ifndef _H_NEO_CORE_CLOCK_
#define _H_NEO_CORE_CLOCK_
#include "neojs/core/allocator.h"
#include <stdbool.h>
#include <stdint.h>
#include <unicode/umachine.h>
#ifdef __cplusplus
extern "C" {
#endif

int32_t neo_clock_get_timezone();

int64_t neo_clock_get_timestamp();

void neo_clock_sleep(uint64_t timeout);

bool neo_clock_parse(const UChar *source, const UChar *format,
                     int64_t *timestamp, const UChar *tz);

char *neo_clock_to_iso(neo_allocator_t allocator, int64_t timestamep);

char *neo_clock_to_rfc(neo_allocator_t allocator, int64_t timestamep);

#ifdef __cplusplus
};
#endif
#endif