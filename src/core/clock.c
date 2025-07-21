#include "core/clock.h"
#include <bits/time.h>
#ifdef _WIN32
#include <Windows.h>
#pragma comment(lib, "winmm.lib")
#else
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#endif

int32_t neo_clock_get_timezone() {
#ifdef _WIN32
#else
  return timezone;
#endif
}

int64_t neo_clock_get_utc_timestamp() {
#ifdef _WIN32
  FILETIME ft;
  ULARGE_INTEGER li;
  GetSystemTimeAsFileTime(&ft);
  li.LowPart = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  long long milliseconds = li.QuadPart / 10000;
  return milliseconds;
#else
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  long long milliseconds = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
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