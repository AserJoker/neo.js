#include "engine/lib/json.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash_map.h"
#include "core/location.h"
#include "core/position.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/basetype/number.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

static neo_js_variable_t
neo_js_json_variable_stringify(neo_js_context_t ctx, neo_js_variable_t variable,
                               neo_js_variable_t replacer, const wchar_t *space,
                               neo_hash_map_t cache, size_t depth) {
  if (neo_hash_map_has(cache, variable, ctx, ctx)) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Converting circular structure to JSON");
  }
  neo_hash_map_set(cache, variable, variable, ctx, ctx);
  if (neo_js_variable_get_type(variable)->kind >= NEO_JS_TYPE_CALLABLE) {
    return NULL;
  }
  if (neo_js_variable_get_type(variable)->kind >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t to_json = neo_js_context_create_string(ctx, L"toJSON");
    to_json = neo_js_context_get_field(ctx, variable, to_json);
    NEO_JS_TRY_AND_THROW(to_json);
    if (neo_js_variable_get_type(to_json)->kind >= NEO_JS_TYPE_CALLABLE) {
      variable = neo_js_context_call(ctx, to_json, variable, 0, NULL);
      NEO_JS_TRY_AND_THROW(variable);
    }
    if (neo_js_context_has_internal(ctx, variable, L"[[primitive]]")) {
      variable = neo_js_context_to_primitive(ctx, variable, L"default");
      NEO_JS_TRY_AND_THROW(variable);
    }
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_SYMBOL) {
    return NULL;
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_BIGINT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"BigInt value can't be serialized in JSON");
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_NULL) {
    return neo_js_context_create_string(ctx, L"null");
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_UNDEFINED) {
    return NULL;
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_NUMBER) {
    neo_js_number_t num = neo_js_variable_to_number(variable);
    if (isnan(num->number) || isinf(num->number)) {
      return NULL;
    }
    return neo_js_context_to_string(ctx, variable);
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_BOOLEAN) {
    return neo_js_context_to_string(ctx, variable);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t str = neo_js_variable_to_string(variable);
    wchar_t *encoded = neo_wstring_encode(allocator, str->string);
    neo_js_context_defer_free(ctx, encoded);
    size_t len = wcslen(str->string) + 3;
    wchar_t *ss = neo_js_context_alloc(ctx, sizeof(wchar_t) * len, NULL);
    neo_js_context_defer_free(ctx, ss);
    swprintf(ss, len, L"\"%ls\"", encoded);
    return neo_js_context_create_string(ctx, ss);
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_ARRAY) {
    size_t max = 128;
    wchar_t *s = neo_allocator_alloc(allocator, max * sizeof(wchar_t), NULL);
    s[0] = 0;
    s = neo_wstring_concat(allocator, s, &max, L"[");
    neo_js_variable_t length = neo_js_context_get_field(
        ctx, variable, neo_js_context_create_string(ctx, L"length"));
    neo_js_number_t num = neo_js_variable_to_number(length);
    int64_t len = num->number;
    for (int64_t idx = 0; idx < len; idx++) {
      if (idx != 0) {
        s = neo_wstring_concat(allocator, s, &max, L",");
      }
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t item = neo_js_context_get_field(ctx, variable, key);
      NEO_JS_TRY(item) {
        neo_js_context_defer_free(ctx, s);
        return item;
      }
      item = neo_js_json_variable_stringify(ctx, item, replacer, space, cache,
                                            depth + 1);
      if (item) {
        NEO_JS_TRY(item) {
          neo_js_context_defer_free(ctx, s);
          return item;
        }
        if (replacer) {
          if (neo_js_variable_get_type(replacer)->kind >=
              NEO_JS_TYPE_CALLABLE) {
            neo_js_variable_t argv[] = {key, item};
            item = neo_js_context_call(
                ctx, replacer, neo_js_context_create_undefined(ctx), 2, argv);
            NEO_JS_TRY(item) {
              neo_js_context_defer_free(ctx, s);
              return item;
            }
          } else {
            neo_js_variable_t v_length = neo_js_context_get_field(
                ctx, replacer, neo_js_context_create_string(ctx, L"length"));
            NEO_JS_TRY(v_length) {
              neo_js_context_defer_free(ctx, s);
              return v_length;
            }
            int64_t length = neo_js_variable_to_number(v_length)->number;
            neo_js_variable_t found = NULL;
            for (int64_t idx = 0; idx < length; idx++) {
              neo_js_variable_t current = neo_js_context_get_field(
                  ctx, replacer, neo_js_context_create_number(ctx, idx));
              NEO_JS_TRY(current) {
                neo_js_context_defer_free(ctx, s);
                return current;
              }
              neo_js_variable_t res =
                  neo_js_context_is_equal(ctx, current, key);
              NEO_JS_TRY(res) {
                neo_js_context_defer_free(ctx, s);
                return res;
              }
              if (neo_js_variable_to_boolean(res)->boolean) {
                found = item;
                break;
              }
            }
            item = found;
          }
        }
      }
      if (item) {
        NEO_JS_TRY(item) {
          neo_js_context_defer_free(ctx, s);
          return item;
        }
        neo_js_string_t str = neo_js_variable_to_string(item);
        s = neo_wstring_concat(allocator, s, &max, str->string);
      } else {
        s = neo_wstring_concat(allocator, s, &max, L"null");
      }
    }
    s = neo_wstring_concat(allocator, s, &max, L"]");
    neo_js_context_defer_free(ctx, s);
    return neo_js_context_create_string(ctx, s);
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_OBJECT) {
    size_t max = 128;
    wchar_t *s = neo_allocator_alloc(allocator, max * sizeof(wchar_t), NULL);
    s[0] = 0;
    s = neo_wstring_concat(allocator, s, &max, L"{");
    bool has_variable = false;
    neo_js_variable_t keys = neo_js_context_get_keys(ctx, variable);
    NEO_JS_TRY(keys) {
      neo_js_context_defer_free(ctx, s);
      return keys;
    }
    neo_js_variable_t v_length = neo_js_context_get_field(
        ctx, keys, neo_js_context_create_string(ctx, L"length"));
    int64_t length = neo_js_variable_to_number(v_length)->number;
    for (int64_t idx = 0; idx < length; idx++) {
      neo_js_variable_t v_key = neo_js_context_get_field(
          ctx, keys, neo_js_context_create_number(ctx, idx));
      neo_js_variable_t item = neo_js_context_get_field(ctx, variable, v_key);
      NEO_JS_TRY(item) {
        neo_js_context_defer_free(ctx, s);
        return item;
      }
      item = neo_js_json_variable_stringify(ctx, item, replacer, space, cache,
                                            depth + 1);
      if (item) {
        NEO_JS_TRY(item) {
          neo_js_context_defer_free(ctx, s);
          return item;
        }
        if (replacer) {
          if (neo_js_variable_get_type(replacer)->kind >=
              NEO_JS_TYPE_CALLABLE) {
            neo_js_variable_t argv[] = {v_key, item};
            item = neo_js_context_call(
                ctx, replacer, neo_js_context_create_undefined(ctx), 2, argv);
            NEO_JS_TRY(item) {
              neo_js_context_defer_free(ctx, s);
              return item;
            }
          } else {
            neo_js_variable_t v_length = neo_js_context_get_field(
                ctx, replacer, neo_js_context_create_string(ctx, L"length"));
            NEO_JS_TRY(v_length) {
              neo_js_context_defer_free(ctx, s);
              return v_length;
            }
            int64_t length = neo_js_variable_to_number(v_length)->number;
            neo_js_variable_t found = NULL;
            for (int64_t idx = 0; idx < length; idx++) {
              neo_js_variable_t current = neo_js_context_get_field(
                  ctx, replacer, neo_js_context_create_number(ctx, idx));
              NEO_JS_TRY(current) {
                neo_js_context_defer_free(ctx, s);
                return current;
              }
              neo_js_variable_t res =
                  neo_js_context_is_equal(ctx, current, v_key);
              NEO_JS_TRY(res) {
                neo_js_context_defer_free(ctx, s);
                return res;
              }
              if (neo_js_variable_to_boolean(res)->boolean) {
                found = item;
                break;
              }
            }
            item = found;
          }
        }
      }
      if (item) {
        if (!has_variable) {
          if (space) {
            s = neo_wstring_concat(allocator, s, &max, L"\n");
          }
          has_variable = true;
        }
        if (space) {
          for (size_t idx = 0; idx <= depth; idx++) {
            s = neo_wstring_concat(allocator, s, &max, space);
          }
        }
        const wchar_t *key = neo_js_variable_to_string(v_key)->string;
        s = neo_wstring_concat(allocator, s, &max, L"\"");
        s = neo_wstring_concat(allocator, s, &max, key);
        s = neo_wstring_concat(allocator, s, &max, L"\"");
        s = neo_wstring_concat(allocator, s, &max, L":");
        const wchar_t *s_item = neo_js_variable_to_string(item)->string;
        s = neo_wstring_concat(allocator, s, &max, s_item);
        if (idx != length - 1) {
          s = neo_wstring_concat(allocator, s, &max, L",");
        }
        if (space) {
          s = neo_wstring_concat(allocator, s, &max, L"\n");
        }
      }
    }
    s = neo_wstring_concat(allocator, s, &max, L"}");
    neo_js_context_defer_free(ctx, s);
    return neo_js_context_create_string(ctx, s);
  }
  return neo_js_context_create_simple_error(
      ctx, NEO_JS_ERROR_INTERNAL, L"Cannot convert internal variable to json");
}

static uint32_t neo_js_json_hash(neo_js_variable_t variable, uint32_t max,
                                 neo_js_context_t ctx) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return (intptr_t)value % max;
}

static int32_t neo_js_json_compare(neo_js_variable_t a, neo_js_variable_t b,
                                  neo_js_context_t ctx) {
  intptr_t addr_a = (intptr_t)neo_js_variable_get_value(a);
  intptr_t addr_b = (intptr_t)neo_js_variable_get_value(b);
  if (addr_a > addr_b) {
    return 1;
  } else if (addr_a == addr_b) {
    return 0;
  } else {
    return -1;
  }
}

NEO_JS_CFUNCTION(neo_js_json_stringify) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = false;
  initialize.auto_free_value = false;
  initialize.hash = (neo_hash_fn_t)neo_js_json_hash;
  initialize.compare = (neo_compare_fn_t)neo_js_json_compare;
  neo_hash_map_t cache = neo_create_hash_map(allocator, &initialize);
  neo_js_context_defer_free(ctx, cache);
  neo_js_variable_t variable = NULL;
  if (argc) {
    variable = argv[0];
  } else {
    variable = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t replacer = NULL;
  if (argc > 1 &&
      (neo_js_variable_get_type(argv[1])->kind >= NEO_JS_TYPE_CALLABLE ||
       neo_js_variable_get_type(argv[1])->kind == NEO_JS_TYPE_ARRAY)) {
    replacer = argv[1];
  }
  if (replacer &&
      neo_js_variable_get_type(replacer)->kind >= NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t argv[] = {neo_js_context_create_string(ctx, L""),
                                variable};
    variable = neo_js_context_call(
        ctx, replacer, neo_js_context_create_undefined(ctx), 2, argv);
    NEO_JS_TRY_AND_THROW(variable);
  }
  wchar_t *space = NULL;
  if (argc > 2) {
    neo_js_variable_t v_space =
        neo_js_context_to_primitive(ctx, argv[2], L"default");
    if (neo_js_variable_get_type(v_space)->kind == NEO_JS_TYPE_NUMBER) {
      v_space = neo_js_context_to_integer(ctx, v_space);
      NEO_JS_TRY_AND_THROW(v_space);
      int64_t len = neo_js_variable_to_number(v_space)->number;
      if (len >= 1 && len <= 10) {
        space = neo_js_context_alloc(ctx, sizeof(wchar_t) * (len + 1), NULL);
        neo_js_context_defer_free(ctx, space);
        for (int64_t idx = 0; idx < len; idx++) {
          space[idx] = L' ';
        }
        space[len] = 0;
      }
    } else {
      v_space = neo_js_context_to_string(ctx, v_space);
      NEO_JS_TRY_AND_THROW(v_space);
      const wchar_t *s = neo_js_variable_to_string(v_space)->string;
      size_t len = wcslen(s);
      if (len > 10) {
        len = 10;
      }
      space = neo_js_context_alloc(ctx, sizeof(wchar_t) * len + 1, NULL);
      neo_js_context_defer_free(ctx, space);
      wcsncpy(space, s, len);
      space[len] = 0;
    }
  }
  variable =
      neo_js_json_variable_stringify(ctx, variable, replacer, space, cache, 0);
  if (!variable) {
    return neo_js_context_create_undefined(ctx);
  }
  return variable;
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

static bool neo_js_json_read_number(neo_position_t *position) {
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

static bool neo_js_json_read_string(neo_position_t *position) {
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

#define THROW_JSON_ERROR(ctx, message, ...)                                    \
  do {                                                                         \
    wchar_t msg[1024];                                                         \
    swprintf(msg, 1024, message, ##__VA_ARGS__);                               \
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX, msg);  \
  } while (0)

neo_js_variable_t neo_js_json_read_variable(neo_js_context_t ctx,
                                            neo_position_t *position,
                                            neo_js_variable_t receiver,
                                            const wchar_t *file);

static neo_js_variable_t neo_js_json_read_array(neo_js_context_t ctx,
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
        THROW_JSON_ERROR(
            ctx, L"Invalid or unexpected token. \n  at _.compile (%ls:%d:%d)",
            file, current.line, current.column);
      }
    }
  }
  current.offset++;
  current.column++;
  *posistion = current;
  return result;
}

static neo_js_variable_t neo_js_json_read_object(neo_js_context_t ctx,
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
        THROW_JSON_ERROR(
            ctx, L"Invalid or unexpected token. \n  at _.compile (%ls:%d:%d)",
            file, current.line, current.column);
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
        THROW_JSON_ERROR(
            ctx, L"Invalid or unexpected token. \n  at _.compile (%ls:%d:%d)",
            file, current.line, current.column);
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
        THROW_JSON_ERROR(
            ctx, L"Invalid or unexpected token. \n  at _.compile (%ls:%d:%d)",
            file, current.line, current.column);
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
      THROW_JSON_ERROR(
          ctx, L"Invalid or unexpected token. \n  at _.compile (%ls:%d:%d)",
          file, current.line, current.column);
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
      THROW_JSON_ERROR(
          ctx, L"Invalid or unexpected token. \n  at _.compile (%ls:%d:%d)",
          file, current.line, current.column);
    }
  } else {
    THROW_JSON_ERROR(
        ctx, L"Invalid or unexpected token. \n  at _.compile (%ls:%d:%d)", file,
        0, 0);
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