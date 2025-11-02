#ifndef _H_NEO_ENGINE_BIGINT_
#define _H_NEO_ENGINE_BIGINT_
#include "core/allocator.h"
#include "core/bigint.h"
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_bigint_t {
  struct _neo_js_value_t super;
  neo_bigint_t value;
};
typedef struct _neo_js_bigint_t *neo_js_bigint_t;
neo_js_bigint_t neo_create_js_bigint(neo_allocator_t allocator,
                                     neo_bigint_t value);
void neo_init_js_bigint(neo_js_bigint_t self, neo_allocator_t allocaotr,
                        neo_bigint_t value);
void neo_deinit_js_bigint(neo_js_bigint_t self, neo_allocator_t allocaotr);
neo_js_value_t neo_js_bigint_to_value(neo_js_bigint_t self);
#ifdef __cplusplus
}
#endif
#endif