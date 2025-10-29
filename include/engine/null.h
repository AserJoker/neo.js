#ifndef _H_NEO_ENGINE_NULL_
#define _H_NEO_ENGINE_NULL_
#include "core/allocator.h"
#include "engine/variable.h"
struct _neo_js_null_t {
  struct _neo_js_value_t super;
};
typedef struct _neo_js_null_t *neo_js_null_t;
neo_js_null_t neo_create_js_null(neo_allocator_t allocator);
#endif