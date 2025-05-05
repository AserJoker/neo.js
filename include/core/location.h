#ifndef _H_NOIX_CORE_LOCATION_
#define _H_NOIX_CORE_LOCATION_

#include "position.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _noix_location_t {
  noix_position_t begin;
  noix_position_t end;
  const char *file;
} noix_location_t;

#ifdef __cplusplus
};
#endif
#endif