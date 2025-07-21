#include "engine/std/array_iterator.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stddef.h>
#include <stdint.h>

neo_js_variable_t neo_js_array_iterator_constructor(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    uint32_t argc,
                                                    neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_array_iterator_next(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {
  neo_js_variable_t vidx = neo_js_context_get_internal(ctx, self, L"[[index]]");
  neo_js_variable_t array =
      neo_js_context_get_internal(ctx, self, L"[[array]]");
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, array, neo_js_context_create_string(ctx, L"length"));
  int64_t length = neo_js_variable_to_number(vlength)->number;
  int64_t index = neo_js_variable_to_number(vidx)->number;
  if (index < length) {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"value"),
                             neo_js_context_get_field(ctx, array, vidx));
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"done"),
                             neo_js_context_create_boolean(ctx, false));
    neo_js_variable_to_number(vidx)->number += 1;
    return result;
  } else {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"value"),
                             neo_js_context_create_undefined(ctx));

    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"done"),
                             neo_js_context_create_boolean(ctx, true));
    return result;
  }
}

neo_js_variable_t neo_js_array_iterator_iterator(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 uint32_t argc,
                                                 neo_js_variable_t *argv) {
  return self;
}