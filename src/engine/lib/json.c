#include "engine/lib/json.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
NEO_JS_CFUNCTION(neo_js_json_stringify) {
  return neo_js_context_create_string(ctx, L"");
}

void neo_js_json_skip_invisible(neo_position_t *position) {
  for (;;) {
    if (*position->offset == ' ' || *position->offset == '\t') {
      position->offset++;
      position->column++;
    } else if (*position->offset == '\n') {
      position->offset++;
      position->column = 0;
      position->line++;
    } else if (*position->offset == '\r') {
      position->offset++;
    } else {
      break;
    }
  }
}

bool neo_js_json_read_number(neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset == '-') {
    current.offset++;
    current.column++;
    if (*current.offset < '0' || *current.offset > '9') {
      *position = current;
      return false;
    }
  }
  while (*current.offset >= '0' && *current.offset <= '9') {
    current.offset++;
    current.column++;
  }
  if (*current.offset == '.') {
    current.offset++;
    current.column++;
    if (*current.offset < '0' || *current.offset > '9') {
      *position = current;
      return false;
    }
    while (*current.offset >= '0' && *current.offset <= '9') {
      current.offset++;
      current.column++;
    }
  }
  if (*current.offset == 'e' || *current.offset == 'E') {
    current.offset++;
    current.column++;
    if (*current.offset < '0' || *current.offset > '9') {
      *position = current;
      return false;
    }
    while (*current.offset >= '0' && *current.offset <= '9') {
      current.offset++;
      current.column++;
    }
  }
  *position = current;
  return true;
}

bool neo_js_json_read_string(neo_position_t *position) {
  neo_position_t current = *position;
  current.offset++;
  current.column++;
  for (;;) {
    if (*current.offset == '\0' || *current.offset == '\n' ||
        *current.offset == '\r') {
      *position = current;
      return false;
    }
    if (*current.offset == '\"') {
      current.offset++;
      current.column++;
      break;
    } else if (*current.offset == '\\') {
      current.offset += 2;
      current.column += 2;
      continue;
    } else {
      current.offset++;
      current.column++;
    }
  }
  *position = current;
  return true;
}
neo_js_variable_t neo_js_json_read_variable(neo_js_context_t ctx,
                                            neo_position_t *position,
                                            neo_js_variable_t receiver,
                                            const wchar_t *file);

neo_js_variable_t neo_js_json_read_array(neo_js_context_t ctx,
                                         neo_position_t *posistion,
                                         neo_js_variable_t receiver,
                                         const wchar_t *file) {
  neo_position_t current = *posistion;
  current.offset++;
  current.line++;
  neo_js_json_skip_invisible(&current);
  size_t idx = 0;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  if (*current.offset != ']') {
    for (;;) {
      neo_js_json_skip_invisible(&current);
      neo_position_t item_current = current;
      neo_js_variable_t item =
          neo_js_json_read_variable(ctx, &item_current, receiver, file);
      neo_location_t loc = {current, item_current, file};
      current = item_current;
      wchar_t *src = neo_location_get(allocator, loc);
      neo_js_context_defer_free(ctx, src);
      NEO_JS_TRY_AND_THROW(item);
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      idx++;
      if (receiver) {
        neo_js_variable_t context = neo_js_context_create_object(ctx, NULL);
        neo_js_context_set_field(ctx, context,
                                 neo_js_context_create_string(ctx, L"source"),
                                 neo_js_context_create_string(ctx, src));
        neo_js_variable_t argv[] = {key, item, context};
        item = neo_js_context_call(ctx, receiver, result, 3, argv);
        NEO_JS_TRY_AND_THROW(item);
      }
      neo_js_context_set_field(ctx, result, key, item);
      neo_js_json_skip_invisible(&current);
      if (*current.offset == ']') {
        break;
      } else if (*current.offset != ',') {
        current.offset++;
        current.column++;
      } else {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_SYNTAX, L"Invalid or unexpected token");
      }
    }
  }
  current.offset++;
  current.column++;
  *posistion = current;
  return result;
}

neo_js_variable_t neo_js_json_read_object(neo_js_context_t ctx,
                                          neo_position_t *posistion,
                                          neo_js_variable_t receiver,
                                          const wchar_t *file) {
  neo_position_t current = *posistion;
  current.offset++;
  current.line++;
  neo_js_json_skip_invisible(&current);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  if (*current.offset != '}') {
    for (;;) {
      neo_js_json_skip_invisible(&current);
      neo_position_t key_position = current;
      if (!neo_js_json_read_string(&key_position)) {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_SYNTAX, L"Invalid or unexpected token");
      }
      neo_location_t key_loc = {current, key_position, file};
      current = key_position;
      wchar_t *key_str = neo_location_get(allocator, key_loc);
      neo_js_context_defer_free(ctx, key_str);
      key_str = neo_wstring_decode(allocator, key_str);
      neo_js_context_defer_free(ctx, key_str);
      key_str[wcslen(key_str) - 1] = 0;
      neo_js_variable_t key = neo_js_context_create_string(ctx, key_str + 1);
      neo_js_json_skip_invisible(&current);
      if (*current.offset != ':') {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_SYNTAX, L"Invalid or unexpected token");
      }
      current.offset++;
      current.column++;
      neo_position_t value_position = current;
      neo_js_json_skip_invisible(&current);
      neo_js_variable_t value =
          neo_js_json_read_variable(ctx, &value_position, receiver, file);
      NEO_JS_TRY_AND_THROW(value);
      neo_location_t value_loc = {current, value_position, file};
      current = value_position;
      if (receiver) {
        wchar_t *value_src = neo_location_get(allocator, value_loc);
        neo_js_context_defer_free(ctx, value_src);
        neo_js_variable_t context = neo_js_context_create_object(ctx, NULL);
        neo_js_context_set_field(ctx, context,
                                 neo_js_context_create_string(ctx, L"source"),
                                 neo_js_context_create_string(ctx, value_src));
        neo_js_variable_t argv[] = {key, value, context};
        value = neo_js_context_call(ctx, receiver, result, 3, argv);
        NEO_JS_TRY_AND_THROW(value);
      }
      NEO_JS_TRY_AND_THROW(neo_js_context_set_field(ctx, result, key, value));
      neo_js_json_skip_invisible(&current);
      if (*current.offset == ',') {
        current.offset++;
        current.column++;
      } else if (*current.offset == '}') {
        break;
      } else {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_SYNTAX, L"Invalid or unexpected token");
      }
    }
  }
  current.offset++;
  current.column++;
  *posistion = current;
  return result;
}

neo_js_variable_t neo_js_json_read_variable(neo_js_context_t ctx,
                                            neo_position_t *position,
                                            neo_js_variable_t receiver,
                                            const wchar_t *file) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_json_skip_invisible(position);
  if (*position->offset == '{') {
    return neo_js_json_read_object(ctx, position, receiver, file);
  } else if (*position->offset == '[') {
    return neo_js_json_read_array(ctx, position, receiver, file);
  } else if (*position->offset == '\"') {
    neo_position_t current = *position;
    if (neo_js_json_read_string(&current)) {
      neo_location_t loc = {*position, current, file};
      *position = current;
      wchar_t *s = neo_location_get(allocator, loc);
      neo_js_context_defer_free(ctx, s);
      wchar_t *decoded = neo_wstring_decode(allocator, s);
      neo_js_context_defer_free(ctx, decoded);
      decoded[wcslen(decoded) - 1] = 0;
      return neo_js_context_create_string(ctx, decoded + 1);
    } else {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX,
                                                L"Invalid or unexpected token");
    }
  } else if (strncmp(position->offset, "null", 4) == 0) {
    position->offset += 4;
    position->column += 4;
    return neo_js_context_create_boolean(ctx, false);
  } else if (strncmp(position->offset, "true", 4) == 0) {
    position->offset += 4;
    position->column += 4;
    return neo_js_context_create_boolean(ctx, false);
  } else if (strncmp(position->offset, "false", 5) == 0) {
    position->offset += 5;
    position->column += 5;
    return neo_js_context_create_boolean(ctx, false);
  } else if ((*position->offset >= '0' && *position->offset <= '9') ||
             *position->offset == '-') {
    neo_position_t current = *position;
    if (neo_js_json_read_number(&current)) {
      neo_location_t loc = {*position, current, file};
      *position = current;
      wchar_t *s = neo_location_get(allocator, loc);
      neo_js_context_defer_free(ctx, s);
      double val = wcstold(s, NULL);
      return neo_js_context_create_number(ctx, val);
    } else {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX,
                                                L"Invalid or unexpected token");
    }
  } else {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX,
                                              L"Invalid or unexpected token");
  }
}

NEO_JS_CFUNCTION(neo_js_json_parse) {
  neo_js_variable_t v_source = NULL;
  if (argc) {
    v_source = argv[0];
  } else {
    v_source = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t receiver = NULL;
  if (argc > 1) {
    receiver = argv[1];
  }
  v_source = neo_js_context_to_string(ctx, v_source);
  NEO_JS_TRY_AND_THROW(v_source);
  const wchar_t *source = neo_js_variable_to_string(v_source)->string;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *utf8_source = neo_wstring_to_string(allocator, source);
  neo_js_context_defer_free(ctx, utf8_source);
  neo_position_t position = {.column = 0, .line = 0, .offset = utf8_source};
  neo_js_variable_t variable =
      neo_js_json_read_variable(ctx, &position, receiver, L"<anonymous_json>");
  NEO_JS_TRY_AND_THROW(variable);
  if (receiver) {
    neo_js_variable_t context = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_field(ctx, context,
                             neo_js_context_create_string(ctx, L"source"),
                             neo_js_context_create_string(ctx, source));
    neo_js_variable_t key = neo_js_context_create_string(ctx, L"");
    neo_js_variable_t argv[] = {key, variable, context};
    variable = neo_js_context_call(ctx, receiver, variable, 3, argv);
  }
  return variable;
}