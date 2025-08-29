#include "engine/basetype/error.h"
#include "core/allocator.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <stddef.h>
#include <string.h>
#include <wchar.h>

neo_js_type_t neo_get_js_error_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_JS_TYPE_ERROR;
  return &type;
}

static void neo_js_error_dispose(neo_allocator_t allocator,
                                 neo_js_error_t self) {
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_error_t neo_create_js_error(neo_allocator_t allocator,
                                   neo_js_variable_t err) {
  neo_js_error_t error = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_error_t), neo_js_error_dispose);
  neo_js_value_init(allocator, &error->value);
  error->value.type = neo_get_js_error_type();
  error->error = neo_js_variable_get_chunk(err);
  return error;
}

neo_js_value_t neo_js_error_to_value(neo_js_error_t self) {
  return &self->value;
}

neo_js_error_t neo_js_value_to_error(neo_js_value_t value) {
  if (value->type == neo_get_js_error_type()) {
    return (neo_js_error_t)value;
  }
  return NULL;
}

neo_js_variable_t neo_js_error_get_error(neo_js_context_t ctx,
                                         neo_js_variable_t self) {
  neo_js_error_t error = neo_js_variable_to_error(self);
  return neo_js_context_create_variable(ctx, error->error, NULL);
}

void neo_js_error_set_error(neo_js_context_t ctx, neo_js_variable_t self,
                            neo_js_variable_t err) {
  neo_js_error_t error = neo_js_variable_to_error(self);
  neo_js_chunk_t herror = neo_js_variable_get_chunk(self);
  neo_js_chunk_t herr = neo_js_variable_get_chunk(err);
  neo_js_chunk_add_parent(
      error->error, neo_js_scope_get_root_chunk(neo_js_context_get_scope(ctx)));
  neo_js_chunk_remove_parent(error->error, herror);
  neo_js_chunk_add_parent(herror, herr);
  error->error = herr;
}