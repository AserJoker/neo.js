#ifndef _H_NEO_ENGINE_CALLABLE_
#define _H_NEO_ENGINE_CALLABLE_
#include "core/allocator.h"
#include "core/map.h"
#include "engine/object.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_callable_t {
  struct _neo_js_object_t super;
  bool is_native;
  bool is_async;
  bool is_generator;
  bool is_lambda;
  bool is_class;
  neo_map_t closure;
  neo_js_value_t bind;
  neo_js_value_t clazz;
};
typedef struct _neo_js_callable_t *neo_js_callable_t;
void neo_init_js_callable(neo_js_callable_t self, neo_allocator_t allocaotr,
                          bool native, bool async, bool generator,
                          neo_js_value_t prototype);
void neo_deinit_js_callable(neo_js_callable_t self, neo_allocator_t allocaotr);
neo_js_value_t neo_js_callable_to_value(neo_js_callable_t self);

#ifdef __cplusplus
}
#endif
#endif