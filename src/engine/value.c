#include "engine/value.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "engine/handle.h"
#include <string.h>

void neo_init_js_value(neo_js_value_t self, neo_allocator_t allocator,
                       neo_js_value_type_t type) {
  neo_init_js_handle(&self->handle, allocator, NEO_JS_HANDLE_VALUE);
  self->type = type;
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = true;
  initialize.auto_free_value = true;
  initialize.compare = (neo_compare_fn_t)strcmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  self->opaque = neo_create_hash_map(allocator, &initialize);
}

void neo_deinit_js_value(neo_js_value_t self, neo_allocator_t allocator) {

  neo_allocator_free(allocator, self->opaque);
  neo_deinit_js_handle(&self->handle, allocator);
}

void neo_js_value_add_parent(neo_js_value_t self, neo_js_value_t parent) {
  neo_js_handle_add_parent(&self->handle, &parent->handle);
}
void neo_js_value_remove_parent(neo_js_value_t self, neo_js_value_t parent) {
  neo_js_handle_remove_parent(&self->handle, &parent->handle);
}