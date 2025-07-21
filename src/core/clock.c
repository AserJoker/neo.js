#include "core/clock.h"
#ifdef _WIN32
#include <Windows.h>
#include <winnt.h>
#pragma comment(lib, "winmm.lib")
#else
#include <bits/time.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#endif

int32_t neo_clock_get_timezone() {
#ifdef _WIN32
  TIME_ZONE_INFORMATION tzInfo;
  DWORD result = GetTimeZoneInformation(&tzInfo);
  switch (result) {
  case TIME_ZONE_ID_STANDARD:
    return tzInfo.StandardBias + tzInfo.Bias;
  case TIME_ZONE_ID_DAYLIGHT:
    return tzInfo.DaylightBias + tzInfo.Bias;
  case TIME_ZONE_ID_UNKNOWN:
    return tzInfo.Bias;
  default:
    return 0;
  }
#else
  tzset();
  return timezone / 60;
#endif
}

int64_t neo_clock_get_timestamp() {
#ifdef _WIN32

  FILETIME ft;
  ULARGE_INTEGER uli;
  GetSystemTimeAsFileTime(&ft);
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  int64_t milliseconds = (uli.QuadPart / 10000) - 11644473600000ULL;
  return milliseconds;
#else
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  int64_t milliseconds = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  return milliseconds;
#endif
}
void neo_clock_sleep(uint64_t timeout) {
#ifdef _WIN32
  Sleep(timeout);
#else
  struct timespec req, rem;
  int ret;

  req.tv_sec = 0;
  req.tv_nsec = 1000000 * timeout;

  while ((ret = nanosleep(&req, &rem)) == -1 && errno == EINTR) {
    req = rem;
  }
#endif
}
neo_time_t neo_clock_resolve(int64_t timestamp, int32_t timezone) {
  timestamp -= timezone * 60000;
  int64_t year = 1970;
  while (timestamp < 0) {
    if (year % 4 == 0) {
      timestamp += 366 * 24 * 3600 * 1000L;
    } else {
      timestamp += 365 * 24 * 3600 * 1000L;
    }
    year--;
  }
  int64_t milliseconds = timestamp % 1000;
  int64_t seconds = timestamp / 1000;
  int64_t days = seconds / 86400;
  seconds %= 86400;
  int64_t hour = seconds / 3600;
  seconds %= 3600;
  int64_t minute = seconds / 60;
  seconds %= 60;
  int64_t weakday = (4 + days) % 7;
  while (days > 366) {
    if (year % 4 == 0) {
      days -= 366;
    } else {
      days -= 365;
    }
    year++;
  }
  if (year % 4 != 0 && days > 365) {
    days -= 365;
    year++;
  }
  int64_t months[] = {
      31, year % 4 == 0 ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
  };
  int64_t month = 0;
  while (days > months[month]) {
    days -= months[month];
    month++;
  }
  neo_time_t time = {
      year,    month + 1,    days + 1, hour,      minute,
      seconds, milliseconds, weakday,  timestamp, timezone,
  };
  return time;
}