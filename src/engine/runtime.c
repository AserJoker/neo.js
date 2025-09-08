#include "engine/runtime.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include <string.h>
#include <wchar.h>
struct _neo_js_runtime_t {
  neo_allocator_t allocator;
  neo_hash_map_t programs;
};
static void neo_js_runtime_dispose(neo_allocator_t allocator,
                                   neo_js_runtime_t runtime) {
  neo_allocator_free(allocator, runtime->programs);
}

neo_js_runtime_t neo_create_js_runtime(neo_allocator_t allocator) {
  neo_js_runtime_t runtime = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_runtime_t), neo_js_runtime_dispose);
  runtime->allocator = allocator;
  neo_hash_map_initialize_t initialize;
  initialize.auto_free_key = true;
  initialize.auto_free_value = true;
  initialize.compare = (neo_compare_fn_t)strcmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  initialize.max_bucket = 1024;
  runtime->programs = neo_create_hash_map(allocator, &initialize);
  return runtime;
}

neo_allocator_t neo_js_runtime_get_allocator(neo_js_runtime_t self) {
  return self->allocator;
}
neo_program_t neo_js_runtime_get_program(neo_js_runtime_t self,
                                         const char *filename) {
  return neo_hash_map_get(self->programs, filename, NULL, NULL);
}

void neo_js_runtime_set_program(neo_js_runtime_t self, const char *filename,
                                neo_program_t program) {
  size_t len = strlen(filename);
  char *key =
      neo_allocator_alloc(self->allocator, sizeof(char) * (len + 1), NULL);
  memset(key, 0, len * sizeof(char));
  strcpy(key, filename);
  neo_hash_map_set(self->programs, key, program, NULL, NULL);
}