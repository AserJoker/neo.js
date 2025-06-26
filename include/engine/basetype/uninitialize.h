#ifndef _H_NEO_ENGINE_BASETYPE_UNINITIALIZE_
#define _H_NEO_ENGINE_BASETYPE_UNINITIALIZE_
#include "core/allocator.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_uninitialize_t *neo_js_uninitialize_t;

struct _neo_js_uninitialize_t {
  struct _neo_js_value_t value;
};

neo_js_type_t neo_get_js_uninitialize_type();

neo_js_uninitialize_t neo_create_js_uninitialize(neo_allocator_t allocator);

#ifdef __cplusplus
}
#endif
#endif