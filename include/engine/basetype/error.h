#ifndef _H_NEO_ENGINE_BASETYPE_ERROR_
#define _H_NEO_ENGINE_BASETYPE_ERROR_
#include "core/allocator.h"
#include "core/list.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_error_t *neo_js_error_t;
typedef enum _neo_js_error_type_t {
  NEO_ERROR_SYNTAX,
  NEO_ERROR_RANGE,
  NEO_ERROR_TYPE,
  NEO_ERROR_REFERENCE,
  NEO_ERROR_CUSTOM
} neo_js_error_type_t;
struct _neo_js_error_t {
  struct _neo_js_value_t value;
  neo_js_error_type_t type;
  wchar_t *message;
  neo_list_t stacktrace;
  neo_js_handle_t custom;
};

neo_js_type_t neo_get_js_error_type();

neo_js_error_t neo_create_js_error(neo_allocator_t allocator,
                                   neo_js_error_type_t type,
                                   const wchar_t *message,
                                   neo_list_t stacktrace);

neo_js_error_t neo_js_value_to_error(neo_js_value_t value);

neo_js_error_type_t neo_js_error_get_type(neo_js_variable_t variable);

const wchar_t *neo_js_error_get_message(neo_js_variable_t variable);

neo_list_t neo_js_error_get_stacktrace(neo_js_variable_t variable);

neo_js_variable_t neo_js_error_get_custom(neo_js_context_t ctx,
                                          neo_js_variable_t self);

void neo_js_error_set_custom(neo_js_context_t ctx, neo_js_variable_t self,
                             neo_js_variable_t custom);

#ifdef __cplusplus
}
#endif
#endif