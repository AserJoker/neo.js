#include "js/runtime.h"
#include "core/allocator.h"
struct _neo_js_runtime_t {
  neo_allocator_t allocator;
};
static void neo_js_runtime_dispose(neo_allocator_t allocator,
                                   neo_js_runtime_t runtime) {}

neo_js_runtime_t neo_create_js_runtime(neo_allocator_t allocator) {
  neo_js_runtime_t runtime = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_runtime_t), neo_js_runtime_dispose);
  runtime->allocator = allocator;
  return runtime;
}

neo_allocator_t neo_js_runtime_get_allocator(neo_js_runtime_t self) {
  return self->allocator;
}