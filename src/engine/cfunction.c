#include "engine/cfunction.h"
#include "core/allocator.h"
#include "engine/function.h"
#include "engine/variable.h"
#include <stdbool.h>
static void neo_js_cfunction_dispose(neo_allocator_t allocator,
                                     neo_js_cfunction_t self) {
  neo_deinit_js_cfunction(self, allocator);
}
neo_js_cfunction_t neo_create_js_cfunction(neo_allocator_t allocator,
                                           neo_js_cfunc_t callee,
                                           neo_js_value_t prototype) {
  neo_js_cfunction_t func = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_cfunction_t), neo_js_cfunction_dispose);
  neo_init_js_cfunction(func, allocator, callee, prototype);
  return func;
}
void neo_init_js_cfunction(neo_js_cfunction_t self, neo_allocator_t allocaotr,
                           neo_js_cfunc_t callee, neo_js_value_t prototype) {
  neo_init_js_function(&self->super, allocaotr, true, false, false, NULL, 0, 0,
                       prototype);
  self->callee = callee;
}
void neo_deinit_js_cfunction(neo_js_cfunction_t self,
                             neo_allocator_t allocaotr) {
  neo_deinit_js_function(&self->super, allocaotr);
}
neo_js_value_t neo_js_cfunction_to_value(neo_js_cfunction_t self) {
  return neo_js_function_to_value(&self->super);
}