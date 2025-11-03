#include "engine/undefined.h"
#include "core/allocator.h"

static void neo_js_undefined_dispose(neo_allocator_t allocator,
                                     neo_js_undefined_t self) {
  neo_deinit_js_undefined(self, allocator);
}

neo_js_undefined_t neo_create_js_undefined(neo_allocator_t allocator) {
  neo_js_undefined_t undefined = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_undefined_t), neo_js_undefined_dispose);
  neo_init_js_undefined(undefined, allocator);
  return undefined;
}

void neo_init_js_undefined(neo_js_undefined_t self, neo_allocator_t allocaotr) {
  neo_init_js_value(&self->super, allocaotr, NEO_JS_TYPE_UNDEFINED);
}

void neo_deinit_js_undefined(neo_js_undefined_t self,
                             neo_allocator_t allocaotr) {
  neo_deinit_js_value(&self->super, allocaotr);
}
neo_js_value_t neo_js_undefined_to_value(neo_js_undefined_t self) {
  return &self->super;
}