#ifndef _H_NEO_ENGINE_UNDEFINED_
#define _H_NEO_ENGINE_UNDEFINED_
#include "core/allocator.h"
#include "engine/variable.h"
struct _neo_js_undefined_t {
  struct _neo_js_value_t super;
};
typedef struct _neo_js_undefined_t *neo_js_undefined_t;
neo_js_undefined_t neo_create_js_undefined(neo_allocator_t allocator);
void neo_init_js_undefined(neo_js_undefined_t self, neo_allocator_t allocaotr);
void neo_deinit_js_undefined(neo_js_undefined_t self,
                             neo_allocator_t allocaotr);
neo_js_value_t neo_js_undefined_to_value(neo_js_undefined_t self);
#endif