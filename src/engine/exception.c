#include "engine/exception.h"
#include "core/allocator.h"
#include "engine/variable.h"
static void neo_js_exception_dispose(neo_allocator_t allocator,
                                     neo_js_exception_t self) {
  neo_deinit_js_exception(self, allocator);
}
neo_js_exception_t neo_create_js_exception(neo_allocator_t allocator,
                                           neo_js_variable_t error) {
  neo_js_exception_t exception = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_exception_t), neo_js_exception_dispose);
  neo_init_js_exception(exception, allocator, error);
  return exception;
}
void neo_init_js_exception(neo_js_exception_t self, neo_allocator_t allocaotr,
                           neo_js_variable_t error) {
  neo_init_js_value(&self->super, allocaotr, NEO_JS_TYPE_EXCEPTION);
  self->error = error;
}
void neo_deinit_js_exception(neo_js_exception_t self,
                             neo_allocator_t allocaotr) {
  neo_deinit_js_value(&self->super, allocaotr);
}
neo_js_value_t neo_js_exception_to_value(neo_js_exception_t self) {
  return &self->super;
}