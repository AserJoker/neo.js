#include "engine/basetype/error.h"
#include "core/allocator.h"
#include "core/list.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/scope.h"
#include "engine/stackframe.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <stddef.h>
#include <string.h>
#include <wchar.h>

neo_js_type_t neo_get_js_error_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_TYPE_ERROR;
  return &type;
}

static void neo_js_error_dispose(neo_allocator_t allocator,
                                 neo_js_error_t self) {
  neo_allocator_free(allocator, self->message);
  neo_allocator_free(allocator, self->stacktrace);
}

neo_js_error_t neo_create_js_error(neo_allocator_t allocator,
                                   neo_js_error_type_t type,
                                   const wchar_t *message,
                                   neo_list_t stacktrace) {
  neo_js_error_t error = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_error_t), neo_js_error_dispose);
  error->value.type = neo_get_js_error_type();
  error->value.ref = 0;
  error->type = type;
  if (message) {
    size_t len = wcslen(message);
    error->message =
        neo_allocator_alloc(allocator, (len + 1) * sizeof(wchar_t), NULL);
    wcscpy(error->message, message);
    error->message[len] = 0;
  } else {
    error->message = NULL;
  }
  neo_list_initialize_t initialize = {true};
  error->stacktrace = neo_create_list(allocator, &initialize);
  for (neo_list_node_t it = neo_list_get_first(stacktrace);
       it != neo_list_get_tail(stacktrace); it = neo_list_node_next(it)) {
    neo_js_stackframe_t stackframe = neo_list_node_get(it);
    neo_js_stackframe_t frame = neo_create_js_stackframe(allocator);
    frame->filename = stackframe->filename;
    frame->line = stackframe->line;
    frame->column = stackframe->column;
    if (stackframe->function) {
      size_t len = wcslen(stackframe->function);
      frame->function =
          neo_allocator_alloc(allocator, sizeof(wchar_t) * (len + 1), NULL);
      wcscpy(frame->function, stackframe->function);
      frame->function[len] = 0;
    }
    neo_list_push(error->stacktrace, frame);
  }
  error->custom = NULL;
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

neo_js_error_type_t neo_js_error_get_type(neo_js_variable_t variable) {
  neo_js_error_t error =
      neo_js_value_to_error(neo_js_variable_get_value(variable));
  return error->type;
}

const wchar_t *neo_js_error_get_message(neo_js_variable_t variable) {
  neo_js_error_t error =
      neo_js_value_to_error(neo_js_variable_get_value(variable));
  return error->message;
}

neo_list_t neo_js_error_get_stacktrace(neo_js_variable_t variable) {
  neo_js_error_t error =
      neo_js_value_to_error(neo_js_variable_get_value(variable));
  return error->stacktrace;
}
neo_js_variable_t neo_js_error_get_custom(neo_js_context_t ctx,
                                          neo_js_variable_t self) {
  neo_js_error_t error = neo_js_variable_to_error(self);
  return neo_js_context_create_variable(ctx, error->custom, NULL);
}

void neo_js_error_set_custom(neo_js_context_t ctx, neo_js_variable_t self,
                             neo_js_variable_t custom) {
  neo_js_error_t error = neo_js_variable_to_error(self);
  neo_js_handle_t herror = neo_js_variable_get_handle(self);
  neo_js_handle_t hcustom = neo_js_variable_get_handle(custom);
  neo_js_handle_add_parent(hcustom, herror);
  if (error->custom) {
    neo_js_handle_add_parent(error->custom, neo_js_scope_get_root_handle(
                                                neo_js_context_get_scope(ctx)));
    neo_js_handle_remove_parent(herror, hcustom);
    error->custom = NULL;
  }
  if (custom) {
    error->custom = hcustom;
  }
}
