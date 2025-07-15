#ifndef _H_NEO_ENGINE_BASETYPE_BIGINT_
#define _H_NEO_ENGINE_BASETYPE_BIGINT_
#include "core/allocator.h"
#include "core/bigint.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_bigint_t *neo_js_bigint_t;

struct _neo_js_bigint_t {
  struct _neo_js_value_t value;
  neo_bigint_t bigint;
};

neo_js_type_t neo_get_js_bigint_type();

neo_js_bigint_t neo_create_js_bigint(neo_allocator_t allocator,
                                     neo_bigint_t value);

neo_js_bigint_t neo_js_value_to_bigint(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif