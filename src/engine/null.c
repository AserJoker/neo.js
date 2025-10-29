#include "engine/null.h"

static void neo_js_null_dispose(neo_allocator_t allocator, neo_js_null_t self) {
}

neo_js_null_t neo_create_js_null(neo_allocator_t allocator) {
  neo_js_null_t null = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_null_t), neo_js_null_dispose);
  null->super.ref = 0;
  null->super.type = NEO_JS_TYPE_NULL;
  return null;
}