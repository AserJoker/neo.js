#ifndef _H_NEO_ENGINE_BASETYPE_ERROR_
#define _H_NEO_ENGINE_BASETYPE_ERROR_
#include "core/allocator.h"
#include "core/list.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_engine_error_t *neo_engine_error_t;

struct _neo_engine_error_t {
  struct _neo_engine_value_t value;
  wchar_t *type;
  wchar_t *message;
  neo_list_t stacktrace;
};

neo_engine_type_t neo_get_js_error_type();

neo_engine_error_t neo_create_js_error(neo_allocator_t allocator,
                                       const wchar_t *type,
                                       const wchar_t *message,
                                       neo_list_t stacktrace);

neo_engine_error_t neo_engine_value_to_error(neo_engine_value_t value);

#ifdef __cplusplus
}
#endif
#endif