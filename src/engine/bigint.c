#include "engine/bigint.h"
#include "core/allocator.h"
#include "engine/variable.h"

static void neo_js_bigint_dispose(neo_allocator_t allocator,
                                  neo_js_bigint_t self) {
  neo_deinit_js_bigint(self, allocator);
}
neo_js_bigint_t neo_create_js_bigint(neo_allocator_t allocator,
                                     neo_bigint_t value) {
  neo_js_bigint_t bigint = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_bigint_t), neo_js_bigint_dispose);
  neo_init_js_bigint(bigint, allocator, value);
  return bigint;
}
void neo_init_js_bigint(neo_js_bigint_t self, neo_allocator_t allocaotr,
                        neo_bigint_t value) {
  neo_init_js_value(&self->super, allocaotr, NEO_JS_TYPE_BIGINT);
  self->value = value;
}
void neo_deinit_js_bigint(neo_js_bigint_t self, neo_allocator_t allocaotr) {
  neo_allocator_free(allocaotr, self->value);
  neo_deinit_js_value(&self->super, allocaotr);
}
neo_js_value_t neo_js_bigint_to_value(neo_js_bigint_t self) {
  return &self->super;
}