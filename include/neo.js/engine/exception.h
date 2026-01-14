#ifndef _H_NEO_ENGINE_EXCEPTION_
#define _H_NEO_ENGINE_EXCEPTION_
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_exception_t *neo_js_exception_t;
struct _neo_js_exception_t {
  struct _neo_js_value_t super;
  neo_js_value_t error;
};
neo_js_exception_t neo_create_js_exception(neo_allocator_t allocator,
                                           neo_js_value_t error);
void neo_init_js_exception(neo_js_exception_t self, neo_allocator_t allocaotr,
                           neo_js_value_t error);
void neo_deinit_js_exception(neo_js_exception_t self,
                             neo_allocator_t allocaotr);
neo_js_value_t neo_js_exception_to_value(neo_js_exception_t self);

#ifdef __cplusplus
}
#endif
#endif