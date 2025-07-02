#include "core/clock.h"
#ifdef _WIN32
#include <Windows.h>
#pragma comment(lib, "winmm.lib")
#else
#include <errno.h>
#include <sys/time.h>
#endif

uint64_t neo_clock_get_timestamp() {
#ifdef _WIN32
  return timeGetTime();
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  unsigned long milliseconds = tv.tv_sec * 1000 + tv.tv_usec / 1000;
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
  req.tv_nsec = 1000000 * timeout; // 1 毫秒

  while ((ret = nanosleep(&req, &rem)) == -1 && errno == EINTR) {
    req = rem;
  }
#endif
}