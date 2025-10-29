#ifndef _H_NEO_ENGINE_STRING_
#define _H_NEO_ENGINE_STRING_
#include "core/allocator.h"
#include "engine/variable.h"
struct _neo_js_string_t {
  struct _neo_js_value_t super;
  uint16_t *value;
};
typedef struct _neo_js_string_t *neo_js_string_t;
neo_js_string_t neo_create_js_string(neo_allocator_t allocator,
                                     uint16_t *value);
void neo_init_js_string(neo_js_string_t self, neo_allocator_t allocator,
                        uint16_t *value);
void neo_deinit_js_string(neo_js_string_t self, neo_allocator_t allocator);
neo_js_value_t neo_js_string_to_value(neo_js_string_t self);
#endif