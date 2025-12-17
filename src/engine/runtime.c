#include "engine/runtime.h"
#include "core/allocator.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include <string.h>
struct _neo_js_runtime_t {
  neo_allocator_t allocator;
  neo_hash_map_t programs;
};

static void neo_js_runtime_dispose(neo_allocator_t allocator,
                                   neo_js_runtime_t self) {
  neo_allocator_free(allocator, self->programs);
}

neo_js_runtime_t neo_create_js_runtime(neo_allocator_t allocator) {
  neo_js_runtime_t rt = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_runtime_t), neo_js_runtime_dispose);
  rt->allocator = allocator;
  neo_hash_map_initialize_t initialize = {0};
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  initialize.compare = (neo_compare_fn_t)strcmp;
  initialize.auto_free_key = false;
  initialize.auto_free_value = true;
  rt->programs = neo_create_hash_map(allocator, &initialize);
  return rt;
}

neo_allocator_t neo_js_runtime_get_allocator(neo_js_runtime_t self) {
  return self->allocator;
}

neo_js_program_t neo_js_runtime_get_program(neo_js_runtime_t self,
                                            const char *filename) {
  return neo_hash_map_get(self->programs, filename);
}
void neo_js_runtime_set_program(neo_js_runtime_t self, const char *filename,
                                neo_js_program_t program) {
  neo_hash_map_set(self->programs, (void *)filename, program);
}