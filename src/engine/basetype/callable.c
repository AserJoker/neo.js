#include "engine/basetype/callable.h"
#include "engine/type.h"

neo_js_callable_t neo_js_value_to_callable(neo_js_value_t value) {
  if (value->type->kind >= NEO_TYPE_CFUNCTION) {
    return (neo_js_callable_t)value;
  }
  return NULL;
}