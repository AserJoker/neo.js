#ifndef _H_NOIX_CORE_POSITION_
#define _H_NOIX_CORE_POSITION_

#include "core/unicode.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _noix_position_t {
  int32_t line;
  int32_t column;
  const char *offset;
} noix_position_t;

noix_utf8_char noix_location_next(noix_position_t *location);

#ifdef __cplusplus
};
#endif
#endif