#include "engine/basetype/interrupt.h"
#include "core/allocator.h"
#include "engine/type.h"
neo_js_type_t neo_get_js_interrupt_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_TYPE_INTERRUPT;
  return &type;
}

neo_js_interrupt_t neo_create_js_interrupt(neo_allocator_t allocator,
                                           neo_js_handle_t result,
                                           size_t offset) {
  neo_js_interrupt_t interrupt =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_interrupt_t), NULL);
  interrupt->value.ref = 0;
  interrupt->value.type = neo_get_js_interrupt_type();
  interrupt->offset = offset;
  interrupt->result = result;
  return interrupt;
}

neo_js_interrupt_t neo_js_value_to_interrupt(neo_js_value_t value) {
  if (value->type->kind == NEO_TYPE_INTERRUPT) {
    return (neo_js_interrupt_t)value;
  }
  return NULL;
}