#include "engine/undefined.h"
#include "core/allocator.h"
#include "engine/variable.h"

static void neo_js_undefined_dispose(neo_allocator_t allocator,
                                     neo_js_undefined_t self) {}

neo_js_undefined_t neo_create_js_undefined(neo_allocator_t allocator) {
  neo_js_undefined_t undefined = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_undefined_t), neo_js_undefined_dispose);
  undefined->super.ref = 0;
  undefined->super.type = NEO_JS_TYPE_UNDEFINED;
  return undefined;
}