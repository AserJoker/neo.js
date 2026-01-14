#ifndef _H_NEO_ENGINE_NUMBER_
#define _H_NEO_ENGINE_NUMBER_
#include "neo.js/core/allocator.h"
#include "neo.js/engine/value.h"


#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_number_t {
  struct _neo_js_value_t super;
  double value;
};
typedef struct _neo_js_number_t *neo_js_number_t;
neo_js_number_t neo_create_js_number(neo_allocator_t allocator, double value);
void neo_init_js_number(neo_js_number_t self, neo_allocator_t allocator,
                        double value);
void neo_deinit_js_number(neo_js_number_t self, neo_allocator_t allocator);
neo_js_value_t neo_js_number_to_value(neo_js_number_t self);

#ifdef __cplusplus
}
#endif
#endif