#ifndef _H_NEO_ENGINE_UNINITIALIZED_
#define _H_NEO_ENGINE_UNINITIALIZED_
#include "core/allocator.h"
#include "engine/value.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _neo_js_uninitialized_t {
  struct _neo_js_value_t super;
};
typedef struct _neo_js_uninitialized_t *neo_js_uninitialized_t;
neo_js_uninitialized_t neo_create_js_uninitialized(neo_allocator_t allocator);
void neo_init_js_uninitialized(neo_js_uninitialized_t self,
                               neo_allocator_t allocaotr);
void neo_deinit_js_uninitialized(neo_js_uninitialized_t self,
                                 neo_allocator_t allocaotr);
neo_js_value_t neo_js_uninitialized_to_value(neo_js_uninitialized_t self);
#ifdef __cplusplus
}
#endif
#endif