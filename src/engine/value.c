#include "engine/value.h"
#include "core/hash.h"
#include "core/hash_map.h"

void neo_js_value_dispose(neo_allocator_t allocaotr, neo_js_value_t value) {
  neo_allocator_free(allocaotr, value->opaque);
}

void neo_js_value_init(neo_allocator_t allocator, neo_js_value_t value) {
  value->ref = 0;
  value->type = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = true;
  initialize.compare = (neo_compare_fn_t)wcscmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  initialize.auto_free_value = true;
  value->opaque = neo_create_hash_map(allocator, &initialize);
}