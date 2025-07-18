#ifndef _H_NEO_ENGINE_BASETYPE_BOOLEAN_
#define _H_NEO_ENGINE_BASETYPE_BOOLEAN_
#include "core/allocator.h"
#include "engine/type.h"
#include "engine/value.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_boolean_t *neo_js_boolean_t;

struct _neo_js_boolean_t {
  struct _neo_js_value_t value;
  bool boolean;
};

neo_js_boolean_t neo_create_js_boolean(neo_allocator_t allocator, bool value);

neo_js_type_t neo_get_js_boolean_type();

neo_js_boolean_t neo_js_value_to_boolean(neo_js_value_t value);
#ifdef __cplusplus
}
#endif
#endif