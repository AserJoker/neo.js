#include "engine/context.h"
#include "core/allocator.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include <string.h>

struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t root_scope;
  neo_js_scope_t current_scope;
};

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t self) {
  self->root_scope = NULL;
  while (self->current_scope) {
    neo_js_context_pop_scope(self);
  }
}

neo_js_context_t neo_create_js_context(neo_js_runtime_t runtime) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root_scope = neo_create_js_scope(allocator, NULL);
  ctx->current_scope = ctx->root_scope;
  return ctx;
}

void neo_js_context_push_scope(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  self->current_scope = neo_create_js_scope(allocator, self->current_scope);
}

void neo_js_context_pop_scope(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_scope_t scope = self->current_scope;
  self->current_scope = neo_js_scope_get_parent(scope);
  neo_allocator_free(allocator, scope);
}
neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self) {
  return self->runtime;
}