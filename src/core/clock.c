#include "core/clock.h"
#include <time.h>
#ifdef __WIN32__
#else
#include <errno.h>
#include <sys/time.h>

#endif

uint64_t neo_clock_get_timestamp() {
#ifdef __WIN32__
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  unsigned long milliseconds = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  return milliseconds;
#endif
}
void neo_clock_sleep(uint64_t timeout) {
#ifdef __WIN32__
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