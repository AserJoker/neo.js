#include "neojs/engine/interrupt.h"
#include "neojs/core/allocator.h"
#include "neojs/engine/handle.h"
#include "neojs/engine/value.h"

static void neo_js_interrupt_dispose(neo_allocator_t allocator,
                                     neo_js_interrupt_t self) {
  neo_deinit_js_interrupt(self, allocator);
}
neo_js_interrupt_t neo_create_js_interrupt(neo_allocator_t allocator,
                                           neo_js_variable_t value,
                                           size_t address,
                                           neo_js_program_t program,
                                           neo_js_vm_t vm, neo_js_scope_t scope,
                                           neo_js_interrupt_type_t type) {
  neo_js_interrupt_t interrupt = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_interrupt_t), neo_js_interrupt_dispose);
  neo_init_js_interrupt(interrupt, allocator, value, address, program, vm,
                        scope, type);
  return interrupt;
}
void neo_init_js_interrupt(neo_js_interrupt_t self, neo_allocator_t allocaotr,
                           neo_js_variable_t value, size_t address,
                           neo_js_program_t program, neo_js_vm_t vm,
                           neo_js_scope_t scope, neo_js_interrupt_type_t type) {
  neo_init_js_value(&self->super, allocaotr, NEO_JS_TYPE_INTERRUPT);
  self->address = address;
  self->value = value->value;
  self->vm = vm;
  self->program = program;
  self->scope = scope;
  self->type = type;
  neo_js_handle_add_parent(&value->value->handle, &self->super.handle);
}
void neo_deinit_js_interrupt(neo_js_interrupt_t self,
                             neo_allocator_t allocaotr) {
  neo_allocator_free(allocaotr, self->vm);
  neo_deinit_js_value(&self->super, allocaotr);
}

neo_js_value_t neo_js_interrupt_to_value(neo_js_interrupt_t self) {
  return &self->super;
}