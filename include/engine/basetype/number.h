#ifndef _H_NEO_ENGINE_BASETYPE_NUMBER_
#define _H_NEO_ENGINE_BASETYPE_NUMBER_
#include "core/allocator.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_engine_number_t *neo_engine_number_t;

struct _neo_engine_number_t {
  struct _neo_engine_value_t value;
  double number;
};

neo_engine_type_t neo_get_js_number_type();

neo_engine_number_t neo_create_js_number(neo_allocator_t allocator,
                                         double value);

neo_engine_number_t neo_engine_value_to_number(neo_engine_value_t value);

#ifdef __cplusplus
}
#endif
#endif