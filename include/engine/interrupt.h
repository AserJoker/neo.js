#ifndef _H_NEO_ENGINE_INTERRUPT_
#define _H_NEO_ENGINE_INTERRUPT_
#include "compiler/program.h"
#include "core/allocator.h"
#include "engine/scope.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_interrupt_t {
  struct _neo_js_value_t super;
  size_t address;
  neo_js_value_t value;
  neo_js_vm_t vm;
  neo_js_scope_t scope;
  neo_program_t program;
};
typedef struct _neo_js_interrupt_t *neo_js_interrupt_t;
neo_js_interrupt_t
neo_create_js_interrupt(neo_allocator_t allocator, neo_js_variable_t value,
                        size_t address, neo_program_t program, neo_js_vm_t vm,
                        neo_js_scope_t scope);
void neo_init_js_interrupt(neo_js_interrupt_t self, neo_allocator_t allocaotr,
                           neo_js_variable_t value, size_t address,
                           neo_program_t program, neo_js_vm_t vm,
                           neo_js_scope_t scope);
void neo_deinit_js_interrupt(neo_js_interrupt_t self,
                             neo_allocator_t allocaotr);
neo_js_value_t neo_js_interrupt_to_value(neo_js_interrupt_t self);

#ifdef __cplusplus
}
#endif
#endif