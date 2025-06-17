#ifndef _H_NEO_JS_BASETYPE_STRING_
#define _H_NEO_JS_BASETYPE_STRING_
#include "core/allocator.h"
#include "js/type.h"
#include "js/value.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_string_t *neo_js_string_t;

struct _neo_js_string_t {
  struct _neo_js_value_t value;
  wchar_t *string;
};

neo_js_type_t neo_get_js_string_type();

neo_js_string_t neo_create_js_string(neo_allocator_t allocator,
                                     const wchar_t *value);

neo_js_string_t neo_js_value_to_string(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif