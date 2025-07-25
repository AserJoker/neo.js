#include "engine/lib/json.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/json.h"
#include "core/list.h"
#include "core/map.h"
#include "core/unicode.h"
#include "core/variable.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <wchar.h>
NEO_JS_CFUNCTION(neo_js_json_stringify) {
  return neo_js_context_create_string(ctx, L"");
}

static neo_js_variable_t neo_variable_to_js_variable(neo_js_context_t ctx,
                                                     neo_variable_t variable) {
  neo_variable_type_t type = neo_variable_get_type(variable);
  switch (type) {
  case NEO_VARIABLE_NIL:
    return neo_js_context_create_null(ctx);
  case NEO_VARIABLE_NUMBER:
    return neo_js_context_create_number(ctx, neo_variable_get_number(variable));
  case NEO_VARIABLE_INTEGER:
    return neo_js_context_create_number(ctx,
                                        neo_variable_get_integer(variable));
  case NEO_VARIABLE_STRING:
    return neo_js_context_create_string(ctx, neo_variable_get_string(variable));
  case NEO_VARIABLE_BOOLEAN:
    return neo_js_context_create_boolean(ctx,
                                         neo_variable_get_boolean(variable));
  case NEO_VARIABLE_ARRAY: {
    neo_js_variable_t array = neo_js_context_create_array(ctx);
    neo_list_t vec = neo_variable_get_array(variable);
    size_t idx = 0;
    for (neo_list_node_t it = neo_list_get_first(vec);
         it != neo_list_get_tail(vec); it = neo_list_node_next(it)) {
      neo_variable_t item = neo_list_node_get(it);
      neo_js_context_set_field(ctx, array,
                               neo_js_context_create_number(ctx, idx),
                               neo_variable_to_js_variable(ctx, item));
      idx += 1;
    }
    return array;
  } break;
  case NEO_VARIABLE_DICT: {
    neo_js_variable_t object = neo_js_context_create_object(ctx, NULL);
    neo_map_t dict = neo_variable_get_dict(variable);
    for (neo_map_node_t it = neo_map_get_first(dict);
         it != neo_map_get_tail(dict); it = neo_map_node_next(it)) {
      const wchar_t *key = neo_map_node_get_key(it);
      neo_variable_t field = neo_map_node_get_value(it);
      neo_js_context_set_field(ctx, object,
                               neo_js_context_create_string(ctx, key),
                               neo_variable_to_js_variable(ctx, field));
    }
    return object;
  } break;
  }
}

NEO_JS_CFUNCTION(neo_js_json_parse) {
  neo_js_variable_t v_source = NULL;
  if (argc) {
    v_source = argv[0];
  } else {
    v_source = neo_js_context_create_undefined(ctx);
  }
  v_source = neo_js_context_to_string(ctx, v_source);
  NEO_JS_TRY_AND_THROW(v_source);
  const wchar_t *source = neo_js_variable_to_string(v_source)->string;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *utf8_source = neo_wstring_to_string(allocator, source);
  neo_js_context_defer_free(ctx, utf8_source);
  neo_variable_t variable =
      TRY(neo_json_parse(allocator, L"<anonymouse_json>", utf8_source)) {
    return neo_js_context_create_compile_error(ctx);
  };
  neo_js_context_defer_free(ctx, variable);
  return neo_variable_to_js_variable(ctx, variable);
}