#ifndef _H_NEO_CORE_CLOCK_
#define _H_NEO_CORE_CLOCK_
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif

int32_t neo_clock_get_timezone();

struct _neo_time_t {
  int64_t year;
  int64_t month;
  int64_t day;
  int64_t hour;
  int64_t minute;
  int64_t second;
  int64_t millisecond;
  int64_t weakday;
  int64_t timestamp;
  int64_t timezone;
  bool invalid;
};

typedef struct _neo_time_t neo_time_t;

int64_t neo_clock_get_timestamp();

neo_time_t neo_clock_resolve(int64_t timestamp, int32_t timezone);

void neo_clock_format(neo_time_t *time);

void neo_clock_sleep(uint64_t timeout);

bool neo_clock_parse_iso(const wchar_t *source, int64_t *timestamp,
                         int64_t *timezone);

bool neo_clock_parse_rfc(const wchar_t *source, int64_t *timestamp,
                         int64_t *timezone);

#ifdef __cplusplus
};
#endif
#endif