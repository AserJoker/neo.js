#ifndef _H_NOIX_COMPILER_LOCATION_
#define _H_NOIX_COMPILER_LOCATION_
#include <stddef.h>
#include <stdint.h>
typedef struct _noix_position_t {
  int32_t line;
  int32_t column;
  const char *offset;
  const char *filename;
} noix_position_t;

typedef struct _noix_location_t {
  noix_position_t start;
  noix_position_t end;
} noix_location_t;

#endif