#include "engine/handle.h"
#include "core/allocator.h"
#include "engine/chunk.h"
struct _neo_js_handle_t {
  neo_js_chunk_t chunk;
  size_t ref;
};

neo_js_handle_t neo_create_js_handle(neo_allocator_t allocator,
                                     neo_js_chunk_t chunk) {
  neo_js_handle_t handle =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_handle_t), NULL);
  handle->chunk = chunk;
  handle->ref = 1;
  return handle;
}

neo_js_chunk_t neo_js_handle_get_chunk(neo_js_handle_t self) {
  return self->chunk;
}

void neo_js_handle_set_chunk(neo_js_handle_t self, neo_js_chunk_t chunk) {
  self->chunk = chunk;
}

size_t *neo_js_handle_get_ref(neo_js_handle_t self) { return &self->ref; }

size_t neo_js_handle_add_ref(neo_js_handle_t self) {
  self->ref++;
  return self->ref;
}
size_t neo_js_handle_release(neo_js_handle_t self) {
  self->ref--;
  return self->ref;
}