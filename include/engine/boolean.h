#ifndef _H_NEO_ENGINE_BOOLEAN_
#define _H_NEO_ENGINE_BOOLEAN_
#include "core/allocator.h"
#include "engine/variable.h"
#include <stdbool.h>
struct _neo_js_boolean_t {
  struct _neo_js_value_t super;
  bool value;
};
typedef struct _neo_js_boolean_t *neo_js_boolean_t;
neo_js_boolean_t neo_create_js_boolean(neo_allocator_t allocator, bool value);
void neo_init_js_boolean(neo_js_boolean_t self, neo_allocator_t allocator,
                         bool value);
void neo_deinit_js_boolean(neo_js_boolean_t self, neo_allocator_t allocator);
neo_js_value_t neo_js_boolean_to_value(neo_js_boolean_t self);
#endif