#include "engine/stackframe.h"
#include "core/allocator.h"
static void neo_js_stackframe_dispose(neo_allocator_t allocator,
                                      neo_js_stackframe_t self) {
  neo_allocator_free(allocator, self->function);
}
neo_js_stackframe_t neo_create_js_stackframe(neo_allocator_t allocator) {
  neo_js_stackframe_t frame =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_stackframe_t),
                          neo_js_stackframe_dispose);
  frame->filename = NULL;
  frame->line = 0;
  frame->column = 0;
  frame->function = NULL;
  return frame;
}