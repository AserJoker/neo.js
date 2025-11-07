#include "engine/function.h"
#include "core/allocator.h"
#include "engine/callable.h"
#include <stdbool.h>
static void neo_js_function_dispose(neo_allocator_t allocator,
                                    neo_js_function_t self) {
  neo_deinit_js_function(self, allocator);
}
neo_js_function_t neo_create_js_function(neo_allocator_t allocator,
                                         neo_program_t program,
                                         neo_js_value_t prototype) {
  neo_js_function_t function = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_function_t), neo_js_function_dispose);
  neo_init_js_function(function, allocator, program, prototype);
  return function;
}
void neo_init_js_function(neo_js_function_t self, neo_allocator_t allocaotr,
                          neo_program_t program, neo_js_value_t prototype) {
  neo_init_js_callable(&self->super, allocaotr, false, false, false, prototype);
  self->source = NULL;
  self->program = program;
  self->address = 0;
}
void neo_deinit_js_function(neo_js_function_t self, neo_allocator_t allocaotr) {
  neo_deinit_js_callable(&self->super, allocaotr);
}
neo_js_value_t neo_js_function_to_value(neo_js_function_t self) {
  return neo_js_callable_to_value(&self->super);
}