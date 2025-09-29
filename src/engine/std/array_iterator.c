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
  neo_js_variable_t vidx = neo_js_context_get_internal(ctx, self, "[[index]]");
  neo_js_variable_t array = neo_js_context_get_internal(ctx, self, "[[array]]");
  neo_js_variable_t vlength =
      neo_js_context_get_string_field(ctx, array, "length");
  int64_t length = neo_js_variable_to_number(vlength)->number;
  int64_t index = neo_js_variable_to_number(vidx)->number;
  if (index < length) {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, "value"),
        neo_js_context_get_field(ctx, array, vidx, NULL), NULL);
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, "done"),
                             neo_js_context_create_boolean(ctx, false), NULL);
    neo_js_variable_to_number(vidx)->number += 1;
    return result;
  } else {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, "value"),
                             neo_js_context_create_undefined(ctx), NULL);

    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, "done"),
                             neo_js_context_create_boolean(ctx, true), NULL);
    return result;
  }
}

neo_js_variable_t neo_js_array_iterator_iterator(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 uint32_t argc,
                                                 neo_js_variable_t *argv) {
  return self;
}

void neo_js_context_init_std_array_iterator(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_string_field(
      ctx, neo_js_context_get_std(ctx).array_iterator_constructor, "prototype");

  neo_js_variable_t to_string_tag = neo_js_context_get_string_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor, "toStringTag");

  neo_js_context_def_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, "ArrayIterator"),
                           true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "next"),
      neo_js_context_create_cfunction(ctx, "next", neo_js_array_iterator_next),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype,
      neo_js_context_get_string_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor, "iterator"),
      neo_js_context_create_cfunction(ctx, "iterator",
                                      neo_js_array_iterator_iterator),
      true, false, true);
}