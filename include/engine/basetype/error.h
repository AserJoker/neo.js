#ifndef _H_NEO_ENGINE_BASETYPE_ERROR_
#define _H_NEO_ENGINE_BASETYPE_ERROR_
#include "core/allocator.h"
#include "core/list.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_error_t *neo_js_error_t;

struct _neo_js_error_t {
  struct _neo_js_value_t value;
  wchar_t *type;
  wchar_t *message;
  neo_list_t stacktrace;
};

neo_js_type_t neo_get_js_error_type();

neo_js_error_t neo_create_js_error(neo_allocator_t allocator,
                                   const wchar_t *type, const wchar_t *message,
                                   neo_list_t stacktrace);

neo_js_error_t neo_js_value_to_error(neo_js_value_t value);

const wchar_t *neo_js_error_get_type(neo_js_variable_t variable);

const wchar_t *neo_js_error_get_message(neo_js_variable_t variable);

neo_list_t neo_js_error_get_stacktrace(neo_js_variable_t variable);

#ifdef __cplusplus
}
#endif
#endif