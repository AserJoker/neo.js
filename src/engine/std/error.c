#include "engine/std/error.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/string.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/stackframe.h"
#include "engine/type.h"
#include "engine/variable.h"

static void neo_js_error_info_dispose(neo_allocator_t allocator,
                                      neo_js_error_info_t self) {
  neo_allocator_free(allocator, self->stacktrace);
}

static neo_js_error_info_t neo_create_js_error_info(neo_allocator_t allocator) {
  neo_js_error_info_t info =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_error_info_t),
                          neo_js_error_info_dispose);
  info->type = NULL;
  neo_list_initialize_t initialzie = {true};
  info->stacktrace = neo_create_list(allocator, &initialzie);
  return info;
}

neo_js_variable_t neo_js_error_constructor(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  if (neo_js_context_get_call_type(ctx) == NEO_JS_FUNCTION_CALL) {
    neo_js_variable_t constructor =
        neo_js_context_get_std(ctx).error_constructor;
    neo_js_variable_t prototype = neo_js_context_get_field(
        ctx, constructor, neo_js_context_create_string(ctx, "prototype"), NULL);
    self = neo_js_context_create_object(ctx, prototype);
    neo_js_context_set_field(ctx, self,
                             neo_js_context_create_string(ctx, "constructor"),
                             constructor, NULL);
  }
  neo_js_variable_t message = NULL;
  if (argc > 0) {
    message = argv[0];
  } else {
    message = neo_js_context_create_string(ctx, "");
  }
  neo_js_variable_t options = NULL;
  if (argc > 1) {
    options = argv[1];
  }
  neo_js_variable_t cause = NULL;
  if (options &&
      neo_js_variable_get_type(options)->kind >= NEO_JS_TYPE_OBJECT) {
    cause = neo_js_context_get_field(
        ctx, options, neo_js_context_create_string(ctx, "cause"), NULL);
  }
  neo_js_context_set_field(
      ctx, self, neo_js_context_create_string(ctx, "message"), message, NULL);
  if (cause) {
    neo_js_context_set_field(
        ctx, self, neo_js_context_create_string(ctx, "cause"), cause, NULL);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_error_info_t info = neo_create_js_error_info(allocator);
  neo_js_context_set_opaque(ctx, self, "info", info);
  neo_list_t stacktrace = neo_js_context_get_stacktrace(ctx, 0, 0);
  for (neo_list_node_t it = neo_list_get_first(stacktrace);
       it != neo_list_get_tail(stacktrace); it = neo_list_node_next(it)) {
    neo_js_stackframe_t stackframe = neo_list_node_get(it);
    neo_js_stackframe_t frame = neo_create_js_stackframe(allocator);
    if (stackframe->filename) {
      frame->filename = neo_create_string(allocator, stackframe->filename);
    } else {
      frame->filename = NULL;
    }
    frame->line = stackframe->line;
    frame->column = stackframe->column;
    frame->function = neo_create_string(allocator, stackframe->function);
    neo_list_push(info->stacktrace, frame);
  }
  return self;
}

neo_js_variable_t neo_js_error_to_string(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_error_info_t info = neo_js_context_get_opaque(ctx, self, "info");
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *result = neo_allocator_alloc(allocator, sizeof(char) * 128, NULL);
  result[0] = 0;
  size_t max = 128;
  if (info->type) {
    result = neo_string_concat(allocator, result, &max, info->type);
  } else {
    result = neo_string_concat(allocator, result, &max, "Error");
  }
  result = neo_string_concat(allocator, result, &max, ": ");
  neo_js_variable_t message = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "message"), NULL);
  neo_js_string_t smessage = neo_js_variable_to_string(message);
  result = neo_string_concat(allocator, result, &max, smessage->string);
  result = neo_string_concat(allocator, result, &max, "\n");
  for (neo_list_node_t it =
           neo_list_node_last(neo_list_get_last(info->stacktrace));
       it != neo_list_get_head(info->stacktrace); it = neo_list_node_last(it)) {
    neo_js_stackframe_t frame = neo_list_node_get(it);
    char tmp[1024];
    if (frame->filename) {
      snprintf(tmp, 1024, "  at %s (%s:%d:%d)\n", frame->function,
               frame->filename, frame->line, frame->column);
    } else {
      snprintf(tmp, 1024, "  at %s (<internal>)\n", frame->function);
    }
    result = neo_string_concat(allocator, result, &max, tmp);
  }
  neo_js_variable_t cause = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "cause"), NULL);
  if (neo_js_variable_get_type(cause)->kind == NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t scause = neo_js_context_to_string(ctx, cause);
    result = neo_string_concat(allocator, result, &max, "caused by:\n");
    neo_js_string_t next = neo_js_variable_to_string(scause);
    result = neo_string_concat(allocator, result, &max, next->string);
  }
  neo_js_variable_t res = neo_js_context_create_string(ctx, result);
  neo_allocator_free(allocator, result);
  return res;
}
void neo_js_context_init_std_error(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).error_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "toString"),
      neo_js_context_create_cfunction(ctx, "toString", neo_js_error_to_string),
      true, false, true);
}