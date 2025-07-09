#include "engine/basetype/coroutine.h"
#include "core/allocator.h"
#include "core/list.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"

neo_js_type_t neo_get_js_coroutine_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_TYPE_COROUTINE;
  return &type;
}

static void neo_js_co_context_dispose(neo_allocator_t allocator,
                                      neo_js_co_context_t ctx) {
  neo_allocator_free(allocator, ctx->vm);
  neo_allocator_free(allocator, ctx->stacktrace);
}

neo_js_co_context_t neo_create_js_co_context(neo_allocator_t allocator,
                                             neo_js_vm_t vm,
                                             neo_program_t program,
                                             neo_list_t stacktrace) {
  neo_js_co_context_t ctx =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_co_context_t),
                          neo_js_co_context_dispose);
  ctx->program = program;
  ctx->result = NULL;
  ctx->vm = vm;
  ctx->running = false;
  ctx->stacktrace = stacktrace;
  return ctx;
}

static void neo_js_coroutine_dispose(neo_allocator_t allocator,
                                     neo_js_coroutine_t self) {
  neo_js_value_dispose(allocator, &self->value);
  neo_allocator_free(allocator, self->ctx);
}

neo_js_coroutine_t neo_create_js_coroutine(neo_allocator_t allocator,
                                           neo_js_co_context_t ctx) {
  neo_js_coroutine_t coroutine = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_coroutine_t), neo_js_coroutine_dispose);
  neo_js_value_init(allocator, &coroutine->value);
  coroutine->value.type = neo_get_js_coroutine_type();
  coroutine->ctx = ctx;
  return coroutine;
}
neo_js_coroutine_t neo_js_value_to_coroutine(neo_js_value_t value) {
  if (value->type->kind == NEO_TYPE_COROUTINE) {
    return (neo_js_coroutine_t)value;
  }
  return NULL;
}

neo_js_co_context_t neo_js_coroutine_get_context(neo_js_variable_t coroutine) {
  neo_js_coroutine_t co = neo_js_variable_to_coroutine(coroutine);
  if (co) {
    return co->ctx;
  }
  return NULL;
}
