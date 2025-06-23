#ifndef _H_NEO_ENGINE_BASETYPE_UNDEFINED_
#define _H_NEO_ENGINE_BASETYPE_UNDEFINED_
#include "core/allocator.h"
#include "engine/type.h"
#include "engine/value.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_engine_undefined_t *neo_engine_undefined_t;

struct _neo_engine_undefined_t {
  struct _neo_engine_value_t value;
};

neo_engine_type_t neo_get_js_undefined_type();

neo_engine_undefined_t neo_create_js_undefined(neo_allocator_t allocator);

neo_engine_undefined_t neo_engine_value_to_undefined(neo_engine_value_t value);

#ifdef __cplusplus
}
#endif
#endif