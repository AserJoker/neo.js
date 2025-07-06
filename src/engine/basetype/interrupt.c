#include "engine/basetype/interrupt.h"
#include "core/allocator.h"
#include "engine/type.h"
neo_js_type_t neo_get_js_interrupt_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_TYPE_INTERRUPT;
  return &type;
}

static void neo_js_interrupt_dispose(neo_allocator_t allocator,
                                     neo_js_interrupt_t self) {
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_interrupt_t neo_create_js_interrupt(neo_allocator_t allocator,
                                           neo_js_handle_t result,
                                           size_t offset,
                                           neo_js_interrupt_type_t type) {
  neo_js_interrupt_t interrupt = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_interrupt_t), neo_js_interrupt_dispose);
  neo_js_value_init(allocator, &interrupt->value);
  interrupt->value.type = neo_get_js_interrupt_type();
  interrupt->offset = offset;
  interrupt->result = result;
  interrupt->type = type;
  return interrupt;
}

neo_js_interrupt_t neo_js_value_to_interrupt(neo_js_value_t value) {
  if (value->type->kind == NEO_TYPE_INTERRUPT) {
    return (neo_js_interrupt_t)value;
  }
  return NULL;
}