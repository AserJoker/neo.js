#ifndef _H_NEO_ENGINE_BASETYPE_ARRAY_
#define _H_NEO_ENGINE_BASETYPE_ARRAY_
#include "core/allocator.h"
#include "engine/basetype/object.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_engine_array_t *neo_engine_array_t;

struct _neo_engine_array_t {
  struct _neo_engine_object_t object;
  size_t length;
};

neo_engine_type_t neo_get_js_array_type();

neo_engine_array_t neo_create_js_array(neo_allocator_t allocator);

neo_engine_array_t neo_engine_value_to_array(neo_engine_value_t value);

#ifdef __cplusplus
}
#endif
#endif