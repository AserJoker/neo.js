#ifndef _H_NEO_ENGINE_BASETYPE_ERROR_
#define _H_NEO_ENGINE_BASETYPE_ERROR_
#include "core/allocator.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_error_t *neo_js_error_t;

struct _neo_js_error_t {
  struct _neo_js_value_t value;
  neo_js_handle_t error;
};

neo_js_type_t neo_get_js_error_type();

neo_js_error_t neo_create_js_error(neo_allocator_t allocator,
                                   neo_js_variable_t error);

neo_js_error_t neo_js_value_to_error(neo_js_value_t value);

neo_js_variable_t neo_js_error_get_error(neo_js_context_t ctx,
                                         neo_js_variable_t self);

void neo_js_error_set_error(neo_js_context_t ctx, neo_js_variable_t self,
                            neo_js_variable_t error);

#ifdef __cplusplus
}
#endif
#endif