#ifndef _H_NEO_ENGINE_VALUE_
#define _H_NEO_ENGINE_VALUE_
#include "neo.js/core/allocator.h"
#include "neo.js/core/hash_map.h"
#include "neo.js/engine/handle.h"
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif
typedef enum _neo_js_value_type_t {
  NEO_JS_TYPE_SIGNAL,
  NEO_JS_TYPE_INTERRUPT,
  NEO_JS_TYPE_EXCEPTION,
  NEO_JS_TYPE_UNINITIALIZED,
  NEO_JS_TYPE_UNDEFINED,
  NEO_JS_TYPE_NULL,
  NEO_JS_TYPE_NUMBER,
  NEO_JS_TYPE_BIGINT,
  NEO_JS_TYPE_BOOLEAN,
  NEO_JS_TYPE_STRING,
  NEO_JS_TYPE_SYMBOL,
  NEO_JS_TYPE_OBJECT,
  NEO_JS_TYPE_CLASS,
  NEO_JS_TYPE_ARRAY,
  NEO_JS_TYPE_FUNCTION
} neo_js_value_type_t;

struct _neo_js_value_t {
  struct _neo_js_handle_t handle;
  neo_js_value_type_t type;
  neo_hash_map_t opaque;
};

typedef struct _neo_js_value_t *neo_js_value_t;

void neo_init_js_value(neo_js_value_t self, neo_allocator_t allocator,
                       neo_js_value_type_t type);
void neo_deinit_js_value(neo_js_value_t self, neo_allocator_t allocator);
void neo_js_value_add_parent(neo_js_value_t self, neo_js_value_t parent);
void neo_js_value_remove_parent(neo_js_value_t self, neo_js_value_t parent);
#ifdef __cplusplus
}
#endif
#endif