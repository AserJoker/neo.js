#ifndef _H_NEO_ENGINE_BASETYPE_STRING_
#define _H_NEO_ENGINE_BASETYPE_STRING_
#include "core/allocator.h"
#include "engine/type.h"
#include "engine/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_string_t *neo_js_string_t;

struct _neo_js_string_t {
  struct _neo_js_value_t value;
  uint16_t *string;
};

neo_js_type_t neo_get_js_string_type();

neo_js_string_t neo_create_js_string(neo_allocator_t allocator,
                                     const char *value);

void neo_js_string_set_cstring(neo_allocator_t allocator,
                               neo_js_variable_t variable, const char *value);

char *neo_js_string_to_cstring(neo_allocator_t allocator,
                               neo_js_variable_t variable);

neo_js_string_t neo_js_value_to_string(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif