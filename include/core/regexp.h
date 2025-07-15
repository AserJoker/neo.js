#ifndef _H_NEO_CORE_REGEXP_
#define _H_NEO_CORE_REGEXP_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define NEO_REGEXP_FLAG_GLOBAL (1 << 1)
#define NEO_REGEXP_FLAG_IGNORECASE (1 << 2)
#define NEO_REGEXP_FLAG_MULTILINE (1 << 3)
#define NEO_REGEXP_FLAG_DOTALL (1 << 4)
#define NEO_REGEXP_FLAG_UNICODE (1 << 5)
#define NEO_REGEXP_FLAG_STICKY (1 << 6)

#ifdef __cplusplus
};
#endif
#endif