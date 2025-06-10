#ifndef _H_NEO_JS_STRING_
#define _H_NEO_JS_STRING_
#include "core/allocator.h"
#include "js/type.h"
#include "js/value.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_string_t *neo_js_string_t;

neo_js_type_t neo_get_js_string_type();

neo_js_string_t neo_create_js_string(neo_allocator_t allocator,
                                     const wchar_t *value);

neo_js_value_t neo_js_string_to_value(neo_js_string_t self);

neo_js_string_t neo_js_value_to_string(neo_js_value_t value);

const wchar_t *neo_js_string_get_value(neo_js_string_t self);

#ifdef __cplusplus
}
#endif
#endif