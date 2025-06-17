#ifndef _H_NEO_JS_BASETYPE_OBJECT_
#define _H_NEO_JS_BASETYPE_OBJECT_
#include "core/allocator.h"
#include "core/hash_map.h"
#include "js/handle.h"
#include "js/type.h"
#include "js/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_object_t *neo_js_object_t;

typedef struct _neo_js_object_property_t {
  neo_js_handle_t value;
  neo_js_handle_t get;
  neo_js_handle_t set;
  bool configurable;
  bool writable;
  bool enumerable;
} *neo_js_object_property_t;

struct _neo_js_object_t {
  struct _neo_js_value_t value;
  neo_hash_map_t properties;
  neo_js_handle_t prototype;
  bool sealed;
  bool frozen;
  bool extensible;
};

neo_js_type_t neo_get_js_object_type();

int8_t neo_js_object_compare_key(neo_js_handle_t handle1,
                                 neo_js_handle_t handle2, neo_js_context_t ctx);

uint32_t neo_js_object_key_hash(neo_js_handle_t handle, uint32_t max_bucket);

neo_js_object_t neo_create_js_object(neo_allocator_t allocator);

neo_js_object_t neo_js_value_to_object(neo_js_value_t value);

neo_js_object_property_t neo_js_object_get_property(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    neo_js_variable_t field);

neo_js_object_property_t
neo_js_object_get_own_property(neo_js_context_t ctx, neo_js_variable_t self,
                               neo_js_variable_t field);

#ifdef __cplusplus
}
#endif
#endif