#include "engine/null.h"

static void neo_js_null_dispose(neo_allocator_t allocator, neo_js_null_t self) {
  neo_deinit_js_null(self, allocator);
}

void neo_init_js_null(neo_js_null_t self, neo_allocator_t allocaotr) {
  neo_init_js_value(&self->super, allocaotr, NEO_JS_TYPE_NULL);
}

void neo_deinit_js_null(neo_js_null_t self, neo_allocator_t allocaotr) {
  neo_deinit_js_value(&self->super, allocaotr);
}

neo_js_value_t neo_js_null_to_value(neo_js_null_t self) { return &self->super; }

neo_js_null_t neo_create_js_null(neo_allocator_t allocator) {
  neo_js_null_t null = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_null_t), neo_js_null_dispose);
  neo_init_js_null(null, allocator);
  return null;
}