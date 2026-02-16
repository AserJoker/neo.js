#include "neojs/core/clock.h"
#include "neojs/core/allocator.h"
#include <stdbool.h>
#include <unicode/calendar.h>
#include <unicode/ucal.h>
#include <unicode/udat.h>
#include <unicode/udata.h>
#include <unicode/uloc.h>
#include <unicode/ulocale.h>
#include <unicode/umachine.h>
#include <unicode/urename.h>
#include <unicode/utypes.h>
#include <unicode/ustring.h>

#ifdef _WIN32
#include <Windows.h>
#include <winnt.h>
#pragma comment(lib, "winmm.lib")
#else
#include <bits/time.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
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

static const char *week_names[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};
static const char *month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};
static const char *zone_names[] = {
    "UTC", "GMT", "EST", "EDT", "CST", "CDT", "MST", "MDT", "PST", "PDT",
};
static int32_t zone_value[] = {
    0,       0,       -5 * 60, -4 * 60, -6 * 60,
    -5 * 60, -7 * 60, -6 * 60, -8 * 60, -7 * 60,
};

int32_t neo_clock_get_timezone() {
  UErrorCode status = U_ZERO_ERROR;
  UCalendar *cal = ucal_open(NULL, 0, NULL, UCAL_TRADITIONAL, &status);
  if (U_FAILURE(status)) {
    return 0;
  }
  int32_t rawOffsetMillis = 0;
  int32_t dstOffsetMillis = 0;
  ucal_getTimeZoneOffsetFromLocal(cal, UCAL_TZ_LOCAL_FORMER, true,
                                  &rawOffsetMillis, &dstOffsetMillis, &status);
  if (U_FAILURE(status)) {
    return 0;
  }
  int32_t totalOffsetMillis = rawOffsetMillis + dstOffsetMillis;
  int32_t offsetMinutes = (totalOffsetMillis / (1000 * 60));
  return offsetMinutes;
}

int64_t neo_clock_get_timestamp() { return (int64_t)ucal_getNow(); }

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

bool neo_clock_parse(const UChar *source, const UChar *format,
                     int64_t *timestamp, const UChar *tz) {
  UErrorCode status = U_ZERO_ERROR;
  UDate result;
  UDateFormat *fmt =
      udat_open(UDAT_PATTERN, UDAT_PATTERN, "en", tz, -1, format, -1, &status);
  if (U_FAILURE(status)) {
    return false;
  }
  result = udat_parse(fmt, source, -1, NULL, &status);
  if (U_FAILURE(status)) {
    udat_close(fmt);
    return false;
  }
  udat_close(fmt);
  *timestamp = result;
  return true;
}

UChar *neo_clock_to_iso(neo_allocator_t allocator, int64_t timestamep) {
  UErrorCode status = U_ZERO_ERROR;
  const char cformat[] = "yyyy-MM-dd'T'HH:mm:ss.SSS'Z'";
  UChar uformat[sizeof(cformat)];
  u_uastrcpy(uformat, cformat);
  UChar *result = neo_allocator_alloc(allocator, sizeof(UChar) * 256, NULL);
  UDateFormat *fmt = udat_open(UDAT_PATTERN, UDAT_PATTERN, NULL, u"UTC", -1,
                               uformat, -1, &status);
  if (U_FAILURE(status)) {
    return NULL;
  }
  udat_format(fmt, timestamep, result, 256, NULL, &status);
  if (U_FAILURE(status)) {
    udat_close(fmt);
    return NULL;
  }
  udat_close(fmt);
  return result;
}

UChar *neo_clock_to_rfc(neo_allocator_t allocator, int64_t timestamep) {
  UErrorCode status = U_ZERO_ERROR;
  UChar uformat[] = u"EEE MMM dd yyyy HH:mm:ss 'GMP'Z (zzzz)";
  UChar *result = neo_allocator_alloc(allocator, sizeof(UChar) * 256, NULL);
  UCalendar *cal = ucal_open(NULL, 0, NULL, UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return NULL;
  }
  UChar zone[256];
  ucal_getTimeZoneID(cal, zone, 256, &status);
  if (U_FAILURE(status)) {
    return NULL;
  }
  ucal_close(cal);
  UDateFormat *fmt = udat_open(UDAT_PATTERN, UDAT_PATTERN, "en", zone, -1,
                               uformat, -1, &status);
  if (U_FAILURE(status)) {
    return NULL;
  }
  udat_format(fmt, timestamep, result, 256, NULL, &status);
  if (U_FAILURE(status)) {
    udat_close(fmt);
    return NULL;
  }
  udat_close(fmt);
  return result;
}
bool neo_clock_to_string(int64_t timestamep, UChar *format, UChar *zone,
                         UChar *result, size_t len) {
  UErrorCode status = U_ZERO_ERROR;
  UDateFormat *fmt = udat_open(UDAT_PATTERN, UDAT_PATTERN, "en", zone, -1,
                               format, -1, &status);
  if (U_FAILURE(status)) {
    return false;
  }
  udat_format(fmt, timestamep, result, len, NULL, &status);
  if (U_FAILURE(status)) {
    udat_close(fmt);
    return false;
  }
  udat_close(fmt);
  return true;
}