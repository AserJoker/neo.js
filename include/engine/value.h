#ifndef _H_NEO_ENGINE_VALUE_
#define _H_NEO_ENGINE_VALUE_
#include "core/allocator.h"
#include "core/hash_map.h"
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*neo_on_dispose_fn_t)(void *ctx);
struct _neo_js_value_t {
  neo_js_type_t type;
  neo_hash_map_t opaque;
  neo_on_dispose_fn_t on_dispose;
  void *dispos_ctx;
};

typedef struct _neo_js_value_t *neo_js_value_t;

void neo_js_value_init(neo_allocator_t allocator, neo_js_value_t value);

void neo_js_value_dispose(neo_allocator_t allocaotr, neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif