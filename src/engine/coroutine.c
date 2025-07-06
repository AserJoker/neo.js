#include "engine/coroutine.h"

static void neo_js_coroutine_dispose(neo_allocator_t allocator,
                                     neo_js_coroutine_t coroutine) {
  neo_allocator_free(allocator, coroutine->vm);
}

neo_js_coroutine_t neo_create_js_coroutine(neo_allocator_t allocator) {
  neo_js_coroutine_t coroutine = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_coroutine_t), neo_js_coroutine_dispose);
  coroutine->value = NULL;
  coroutine->root = NULL;
  coroutine->scope = NULL;
  coroutine->vm = NULL;
  coroutine->program = NULL;
  coroutine->running = false;
  return coroutine;
}