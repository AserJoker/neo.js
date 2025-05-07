#ifndef _H_NEO_CORE_POSITION_
#define _H_NEO_CORE_POSITION_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_position_t {
  int32_t line;
  int32_t column;
  const char *offset;
} neo_position_t;

#ifdef __cplusplus
};
#endif
#endif