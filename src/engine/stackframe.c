#include "engine/stackframe.h"
#include "core/allocator.h"

neo_js_stackframe_t neo_create_js_stackframe(neo_allocator_t allocator,
                                             const uint16_t *filename,
                                             const uint16_t *funcname,
                                             uint32_t line, uint32_t column) {
  neo_js_stackframe_t frame =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_stackframe_t), NULL);
  frame->filename = filename;
  frame->funcname = funcname;
  frame->column = column;
  frame->line = line;
  return frame;
}