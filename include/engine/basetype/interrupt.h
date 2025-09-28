#ifndef _H_NEO_ENGINE_BASETYPE_INTERRUPT_
#define _H_NEO_ENGINE_BASETYPE_INTERRUPT_
#include "core/allocator.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_interrupt_t *neo_js_interrupt_t;

typedef enum _neo_js_interrupt_type_t {
  NEO_JS_INTERRUPT_AWAIT,
  NEO_JS_INTERRUPT_BACKEND_AWAIT,
  NEO_JS_INTERRUPT_YIELD,
  NEO_JS_INTERRUPT_GOTO,
} neo_js_interrupt_type_t;

struct _neo_js_interrupt_t {
  struct _neo_js_value_t value;
  neo_js_handle_t result;
  size_t offset;
  neo_js_interrupt_type_t type;
};
neo_js_type_t neo_get_js_interrupt_type();

neo_js_interrupt_t neo_create_js_interrupt(neo_allocator_t allocator,
                                           neo_js_handle_t result,
                                           size_t offset,
                                           neo_js_interrupt_type_t type);

neo_js_interrupt_t neo_js_value_to_interrupt(neo_js_value_t value);
#ifdef __cplusplus
}
#endif
#endif