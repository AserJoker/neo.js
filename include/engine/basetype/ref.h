#ifndef _H_NEO_ENGINE_BASETYPE_REF_
#define _H_NEO_ENGINE_BASETYPE_REF_
#include "core/allocator.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_ref_t *neo_js_ref_t;

struct _neo_js_ref_t {
  struct _neo_js_value_t value;
  neo_js_handle_t origin;
};

neo_js_type_t neo_get_js_ref_type();

neo_js_ref_t neo_create_js_ref(neo_allocator_t allocator, neo_js_handle_t origin);

neo_js_ref_t neo_js_value_to_ref(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif