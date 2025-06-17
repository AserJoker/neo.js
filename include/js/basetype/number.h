#ifndef _H_NEO_JS_BASETYPE_NUMBER_
#define _H_NEO_JS_BASETYPE_NUMBER_
#include "core/allocator.h"
#include "js/type.h"
#include "js/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_number_t *neo_js_number_t;

struct _neo_js_number_t {
  struct _neo_js_value_t value;
  double number;
};

neo_js_type_t neo_get_js_number_type();

neo_js_number_t neo_create_js_number(neo_allocator_t allocator, double value);

neo_js_number_t neo_js_value_to_number(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif