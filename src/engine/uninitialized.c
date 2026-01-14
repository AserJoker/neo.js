#include "neo.js/engine/uninitialized.h"
#include "neo.js/core/allocator.h"
#include "neo.js/engine/value.h"

static void neo_js_uninitialized_dispose(neo_allocator_t allocator,
                                         neo_js_uninitialized_t self) {
  neo_deinit_js_uninitialized(self, allocator);
}

neo_js_uninitialized_t neo_create_js_uninitialized(neo_allocator_t allocator) {
  neo_js_uninitialized_t uninitialized =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_uninitialized_t),
                          neo_js_uninitialized_dispose);
  neo_init_js_uninitialized(uninitialized, allocator);
  return uninitialized;
}

void neo_init_js_uninitialized(neo_js_uninitialized_t self,
                               neo_allocator_t allocaotr) {
  neo_init_js_value(&self->super, allocaotr, NEO_JS_TYPE_UNINITIALIZED);
}

void neo_deinit_js_uninitialized(neo_js_uninitialized_t self,
                                 neo_allocator_t allocaotr) {
  neo_deinit_js_value(&self->super, allocaotr);
}
neo_js_value_t neo_js_uninitialized_to_value(neo_js_uninitialized_t self) {
  return &self->super;
}