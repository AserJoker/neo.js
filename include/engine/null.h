#ifndef _H_NEO_ENGINE_NULL_
#define _H_NEO_ENGINE_NULL_
#include "core/allocator.h"
#include "engine/variable.h"
struct _neo_js_null_t {
  struct _neo_js_value_t super;
};
typedef struct _neo_js_null_t *neo_js_null_t;
neo_js_null_t neo_create_js_null(neo_allocator_t allocator);
void neo_init_js_null(neo_js_null_t self, neo_allocator_t allocaotr);
void neo_deinit_js_null(neo_js_null_t self, neo_allocator_t allocaotr);
neo_js_value_t neo_js_null_to_value(neo_js_null_t self);
#endif