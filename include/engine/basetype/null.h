#ifndef _H_NEO_ENGINE_BASETYPE_NULL_
#define _H_NEO_ENGINE_BASETYPE_NULL_
#include "core/allocator.h"
#include "engine/type.h"
#include "engine/value.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_engine_null_t *neo_engine_null_t;

struct _neo_engine_null_t {
  struct _neo_engine_value_t value;
};

neo_engine_type_t neo_get_js_null_type();

neo_engine_null_t neo_create_js_null(neo_allocator_t allocator);

neo_engine_null_t neo_engine_value_to_null(neo_engine_value_t value);

#ifdef __cplusplus
}
#endif
#endif