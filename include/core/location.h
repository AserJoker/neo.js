#ifndef _H_NEO_CORE_LOCATION_
#define _H_NEO_CORE_LOCATION_

#include "position.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_location_t {
  neo_position_t begin;
  neo_position_t end;
  const char *file;
} neo_location_t;

#ifdef __cplusplus
};
#endif
#endif