#ifndef _H_NEO_ENGINE_ARRAY_
#define _H_NEO_ENGINE_ARRAY_
#include "neojs/core/allocator.h"
#include "neojs/engine/object.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_array_t *neo_js_array_t;
struct _neo_js_array_t {
  struct _neo_js_object_t super;
};
neo_js_array_t neo_create_js_array(neo_allocator_t allocator,
                                   neo_js_value_t prototype);
void neo_init_js_array(neo_js_array_t self, neo_allocator_t allocaotr,
                       neo_js_value_t prototype);
void neo_deinit_js_array(neo_js_array_t self, neo_allocator_t allocaotr);
neo_js_value_t neo_js_array_to_value(neo_js_array_t self);
#ifdef __cplusplus
}
#endif
#endif