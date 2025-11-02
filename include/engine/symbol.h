#ifndef _H_NEO_ENGINE_SYMBOL_
#define _H_NEO_ENGINE_SYMBOL_
#include "core/allocator.h"
#include "engine/variable.h"

#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_symbol_t {
  struct _neo_js_value_t super;
  uint16_t *description;
};
typedef struct _neo_js_symbol_t *neo_js_symbol_t;
neo_js_symbol_t neo_create_js_symbol(neo_allocator_t allocator,
                                     const uint16_t *description);
void neo_init_js_symbol(neo_js_symbol_t self, neo_allocator_t allocator,
                        const uint16_t *description);
void neo_deinit_js_symbol(neo_js_symbol_t self, neo_allocator_t allocator);
neo_js_value_t neo_js_symbol_to_value(neo_js_symbol_t self);

#ifdef __cplusplus
}
#endif
#endif