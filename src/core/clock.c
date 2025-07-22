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

static int64_t months[2][12] = {
    {
        31,
        28,
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
    },
    {
        31,
        29,
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
    },
};

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
  int64_t now = timestamp;
  now -= timezone * 60000;
  int64_t year = 1970;
  while (now < 0) {
    if (year % 4 == 0) {
      now += 366 * 24 * 3600 * 1000L;
    } else {
      now += 365 * 24 * 3600 * 1000L;
    }
    year--;
  }
  int64_t milliseconds = now % 1000;
  int64_t seconds = now / 1000;
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
  int64_t month = 0;
  while (days >= months[year % 4 == 0][month]) {
    days -= months[year % 4 == 0][month];
    month++;
  }
  neo_time_t time = {
      year,    month,        days,    hour,      minute,
      seconds, milliseconds, weakday, timestamp, timezone,
  };
  return time;
}

void neo_clock_format(neo_time_t *time) {
  while (time->millisecond < 0) {
    time->second--;
    time->millisecond += 1000;
  }
  while (time->millisecond >= 1000) {
    time->millisecond -= 1000;
    time->second++;
  }
  while (time->second < 0) {
    time->minute--;
    time->second += 60;
  }
  while (time->second >= 60) {
    time->second -= 60;
    time->minute++;
  }
  while (time->minute < 0) {
    time->hour--;
    time->minute += 60;
  }
  while (time->minute >= 60) {
    time->minute -= 60;
    time->hour++;
  }
  while (time->hour < 0) {
    time->day--;
    time->hour += 24;
  }
  while (time->hour >= 24) {
    time->hour -= 24;
    time->day++;
  }
  while (time->day < 0) {
    while (time->month < 0) {
      time->year--;
      time->month += 12;
    }
    time->month--;
    time->day += months[time->year % 4 == 0][time->month];
  }
  while (time->day >= months[time->year % 4 == 0][time->month]) {
    time->day -= months[time->year % 4 == 0][time->month];
    time->month++;
    while (time->month >= 12) {
      time->month -= 12;
      time->year++;
    }
  }
  while (time->month < 0) {
    time->year--;
    time->month += 12;
  }
  while (time->month >= 12) {
    time->month -= 12;
    time->year++;
  }
  int64_t timestamp = 0;
  for (int64_t year = 1970; year > time->year; year--) {
    if (year % 4 == 0) {
      timestamp -= 366;
    } else {
      timestamp -= 365;
    }
  }
  for (int64_t year = 1970; year < time->year; year++) {
    if (year % 4 == 0) {
      timestamp += 366;
    } else {
      timestamp += 365;
    }
  }
  for (int8_t month = 0; month < time->month; month++) {
    timestamp += months[time->year % 4 == 0][month];
  }
  timestamp += time->day;
  timestamp *= 24;
  timestamp += time->hour;
  timestamp *= 60;
  timestamp += time->timezone;
  timestamp += time->minute;
  timestamp *= 60;
  timestamp += time->second;
  timestamp *= 1000;
  timestamp += time->millisecond;
  time->timestamp = timestamp;
}