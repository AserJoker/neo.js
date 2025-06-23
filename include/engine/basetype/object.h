#ifndef _H_NEO_ENGINE_BASETYPE_OBJECT_
#define _H_NEO_ENGINE_BASETYPE_OBJECT_
#include "core/allocator.h"
#include "core/hash_map.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_engine_object_t *neo_engine_object_t;

typedef struct _neo_engine_object_property_t {
  neo_engine_handle_t value;
  neo_engine_handle_t get;
  neo_engine_handle_t set;
  bool configurable;
  bool writable;
  bool enumerable;
} *neo_engine_object_property_t;

struct _neo_engine_object_t {
  struct _neo_engine_value_t value;
  neo_hash_map_t properties;
  neo_hash_map_t internal;
  neo_engine_handle_t prototype;
  neo_engine_handle_t constructor;
  bool sealed;
  bool frozen;
  bool extensible;
};

neo_engine_type_t neo_get_js_object_type();

int8_t neo_engine_object_compare_key(neo_engine_handle_t handle1,
                                     neo_engine_handle_t handle2,
                                     neo_engine_context_t ctx);

uint32_t neo_engine_object_key_hash(neo_engine_handle_t handle,
                                    uint32_t max_bucket);

neo_engine_object_t neo_create_js_object(neo_allocator_t allocator);

neo_engine_object_property_t
neo_create_js_object_property(neo_allocator_t allocator);

neo_engine_object_t neo_engine_value_to_object(neo_engine_value_t value);

neo_engine_object_property_t
neo_engine_object_get_property(neo_engine_context_t ctx,
                               neo_engine_variable_t self,
                               neo_engine_variable_t field);

neo_engine_object_property_t
neo_engine_object_get_own_property(neo_engine_context_t ctx,
                                   neo_engine_variable_t self,
                                   neo_engine_variable_t field);

#ifdef __cplusplus
}
#endif
#endif