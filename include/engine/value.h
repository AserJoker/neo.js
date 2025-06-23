#ifndef _H_NEO_ENGINE_VALUE_
#define _H_NEO_ENGINE_VALUE_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_engine_value_t {
  neo_engine_type_t type;
  uint32_t ref;
};

typedef struct _neo_engine_value_t *neo_engine_value_t;

#ifdef __cplusplus
}
#endif
#endif