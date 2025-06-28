#include "engine/basetype/coroutine.h"
#include "core/allocator.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "engine/type.h"

neo_js_type_t neo_get_js_coroutine_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_TYPE_COROUTINE;
  return &type;
}

void neo_js_coroutine_dispose(neo_allocator_t allocator,
                              neo_js_coroutine_t self) {
  neo_allocator_free(allocator, self->vm);
  if (neo_js_scope_release(self->scope) == 0) {
    neo_allocator_free(allocator, self->scope);
  }
}

neo_js_coroutine_t neo_create_js_coroutine(neo_allocator_t allocator,
                                           neo_js_vm_t vm,
                                           neo_program_t program,
                                           neo_js_scope_t scope) {
  neo_js_coroutine_t coroutine = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_coroutine_t), neo_js_coroutine_dispose);
  coroutine->value.ref = 0;
  coroutine->value.type = neo_get_js_coroutine_type();
  coroutine->vm = vm;
  coroutine->scope = scope;
  coroutine->program = program;
  coroutine->result = NULL;
  coroutine->done = false;
  neo_js_scope_add_ref(scope);
  return coroutine;
}

neo_js_coroutine_t neo_js_value_to_coroutine(neo_js_value_t value) {
  if (value->type->kind == NEO_TYPE_COROUTINE) {
    return (neo_js_coroutine_t)value;
  }
  return NULL;
}