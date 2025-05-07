#ifndef _H_NEO_CORE_LOCATION_
#define _H_NEO_CORE_LOCATION_

#include "position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_location_t {
  neo_position_t begin;
  neo_position_t end;
  const char *file;
} neo_location_t;

bool neo_location_is(neo_location_t loc, const char *str);

#ifdef __cplusplus
};
#endif
#endif