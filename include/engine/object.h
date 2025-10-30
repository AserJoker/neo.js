#ifndef _H_NEO_ENGINE_OBJECT_
#define _H_NEO_ENGINE_OBJECT_
#include "core/allocator.h"
#include "core/hash_map.h"
#include "engine/variable.h"
typedef struct _neo_js_object_property_t *neo_js_object_property_t;
struct _neo_js_object_property_t {
  neo_js_variable_t get;
  neo_js_variable_t set;
  bool configurable;
  bool enumable;

  bool writable;
  neo_js_variable_t value;
};

struct _neo_js_object_t {
  struct _neo_js_value_t super;
  neo_js_variable_t prototype;
  neo_hash_map_t properties;
  bool frozen;
  bool extensible;
  bool sealed;
};

typedef struct _neo_js_object_t *neo_js_object_t;
neo_js_object_t neo_create_js_object(neo_allocator_t allocator,
                                     neo_js_variable_t prototype);
void neo_init_js_object(neo_js_object_t self, neo_allocator_t allocator,
                        neo_js_variable_t prototype);
void neo_deinit_js_object(neo_js_object_t self, neo_allocator_t allocator);

neo_js_value_t neo_js_object_to_value(neo_js_object_t self);

int32_t neo_js_object_key_compare(const neo_js_variable_t self,
                                  const neo_js_variable_t another,
                                  neo_js_context_t ctx);

uint32_t neo_js_object_key_hash(const neo_js_variable_t self, uint32_t max,
                                neo_js_context_t ctx);

neo_js_object_property_t
neo_create_js_object_property(neo_allocator_t allocator);
#endif