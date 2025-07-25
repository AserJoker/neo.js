#include "core/json.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/map.h"
#include "core/position.h"
#include "core/string.h"
#include "core/variable.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

wchar_t *neo_json_stringify(neo_allocator_t allocator,
                            neo_variable_t variable) {
  wchar_t *result = neo_allocator_alloc(allocator, 128, NULL);
  result[0] = 0;
  size_t max = 0;
  switch (neo_variable_get_type(variable)) {
  case NEO_VARIABLE_NIL:
    result = neo_wstring_concat(allocator, result, &max, L"null");
    break;
  case NEO_VARIABLE_NUMBER: {
    wchar_t tmp[16];
    swprintf(tmp, 16, L"%lf", neo_variable_get_number(variable));
    result = neo_wstring_concat(allocator, result, &max, tmp);
    break;
  }
  case NEO_VARIABLE_INTEGER: {
    wchar_t tmp[16];
    swprintf(tmp, 16, L"%ld", neo_variable_get_integer(variable));
    result = neo_wstring_concat(allocator, result, &max, tmp);
    break;
  }
  case NEO_VARIABLE_STRING: {
    const wchar_t *src = neo_variable_get_string(variable);
    size_t len = wcslen(src);
    wchar_t *buf =
        neo_allocator_alloc(allocator, len * 2 * sizeof(wchar_t), NULL);
    wchar_t *dst = buf;
    while (*src) {
      if (*src == '\n') {
        *dst++ = '\\';
        *dst++ = 'n';
      } else if (*src == '\r') {
        *dst++ = '\\';
        *dst++ = 'r';
      } else if (*src == '\t') {
        *dst++ = '\\';
        *dst++ = 't';
      } else {
        *dst++ = *src;
      }
      src++;
    }
    *dst = 0;
    result = neo_wstring_concat(allocator, result, &max, L"\"");
    result = neo_wstring_concat(allocator, result, &max, buf);
    result = neo_wstring_concat(allocator, result, &max, L"\"");
    neo_allocator_free(allocator, buf);
  } break;
  case NEO_VARIABLE_BOOLEAN:
    result = neo_wstring_concat(allocator, result, &max,
                                neo_variable_get_boolean(variable) ? L"true"
                                                                   : L"false");
    break;
  case NEO_VARIABLE_ARRAY: {
    neo_list_t array = neo_variable_get_array(variable);
    result = neo_wstring_concat(allocator, result, &max, L"[");
    for (neo_list_node_t it = neo_list_get_first(array);
         it != neo_list_get_tail(array); it = neo_list_node_next(it)) {
      neo_variable_t item = neo_list_node_get(it);
      if (it != neo_list_get_first(array)) {
        result = neo_wstring_concat(allocator, result, &max, L",");
      }
      wchar_t *itemjson = neo_json_stringify(allocator, item);
      result = neo_wstring_concat(allocator, result, &max, itemjson);
      neo_allocator_free(allocator, itemjson);
    }
    result = neo_wstring_concat(allocator, result, &max, L"]");
  } break;
  case NEO_VARIABLE_DICT: {
    neo_map_t dict = neo_variable_get_dict(variable);
    result = neo_wstring_concat(allocator, result, &max, L"{");
    for (neo_map_node_t it = neo_map_get_first(dict);
         it != neo_map_get_tail(dict); it = neo_map_node_next(it)) {
      wchar_t *key = neo_map_node_get_key(it);
      neo_variable_t item = neo_map_node_get_value(it);
      if (it != neo_map_get_first(dict)) {
        result = neo_wstring_concat(allocator, result, &max, L",");
      }
      result = neo_wstring_concat(allocator, result, &max, L"\"");
      result = neo_wstring_concat(allocator, result, &max, key);
      result = neo_wstring_concat(allocator, result, &max, L"\"");
      result = neo_wstring_concat(allocator, result, &max, L":");
      wchar_t *itemjson = neo_json_stringify(allocator, item);
      result = neo_wstring_concat(allocator, result, &max, itemjson);
      neo_allocator_free(allocator, itemjson);
    }
    result = neo_wstring_concat(allocator, result, &max, L"}");
  } break;
  }
  return result;
}
neo_variable_t neo_json_read_variable(neo_allocator_t allocator,
                                      const wchar_t *file,
                                      neo_position_t *position);

void neo_json_skip_invisible(neo_allocator_t allocator, const wchar_t *file,
                             neo_position_t *position) {
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
void neo_json_read_string(neo_allocator_t allocator, const wchar_t *file,
                          neo_position_t *current) {
  current->offset++;
  current->column++;
  while (*current->offset) {
    if (*current->offset == '\\') {
      current->offset += 2;
      current->column += 2;
    } else if (*current->offset == '\"') {
      current->offset++;
      current->column++;
      break;
    } else if (*current->offset == '\0' || *current->offset == '\n' ||
               *current->offset == '\r') {
      THROW("Invalid or unexpected token \n  at %ls:%d:%d", file, current->line,
            current->column);
      return;
    } else {
      current->column++;
      current->offset++;
    }
  }
}

void neo_json_read_number(neo_allocator_t allocator, const wchar_t *file,
                          neo_position_t *current) {
  if (*current->offset == '-') {
    current->offset++;
    current->column++;
  } else if (*current->offset == '+') {
    current->offset++;
    current->column++;
  }
  while (*current->offset >= '0' && *current->offset <= '9') {
    current->offset++;
    current->column++;
  }
  if (*current->offset == '.') {
    current->offset++;
    current->column++;
    if (*current->offset < '0' || *current->offset > '9') {
      THROW("Invalid or unexpected token \n  at %ls:%d:%d", file, current->line,
            current->column);
    }
    while (*current->offset >= '0' && *current->offset <= '9') {
      current->offset++;
      current->column++;
    }
  }
  if (*current->offset == 'e' || *current->offset == 'E') {
    current->offset++;
    current->column++;
    if ((*current->offset >= '0' && *current->offset <= '9') ||
        ((*current->offset == '+' || *current->offset == '-') &&
         (*(current->offset + 1) >= '0' && *(current->offset + 1) <= '9'))) {
      while (*current->offset >= '0' && *current->offset <= '9') {
        current->offset++;
        current->column++;
      }
      if (*current->offset == '.') {
        current->offset++;
        current->column++;
        if (*current->offset < '0' || *current->offset > '9') {
          THROW("Invalid or unexpected token \n  at %ls:%d:%d", file,
                current->line, current->column);
        }
        while (*current->offset >= '0' && *current->offset <= '9') {
          current->offset++;
          current->column++;
        }
      }
    } else {
      THROW("Invalid or unexpected token \n  at %ls:%d:%d", file, current->line,
            current->column);
    }
  }
}

neo_variable_t neo_json_read_dict(neo_allocator_t allocator,
                                  const wchar_t *file,
                                  neo_position_t *position) {
  neo_variable_t dict = neo_create_variable_dict(allocator, NULL, NULL);
  position->column++;
  position->offset++;
  wchar_t *key = NULL;
  neo_variable_t value = NULL;
  neo_json_skip_invisible(allocator, file, position);
  if (*position->offset != '}') {
    for (;;) {
      neo_position_t current = *position;
      TRY(neo_json_read_string(allocator, file, &current)) { goto onerror; }
      neo_location_t loc = {*position, current, file};
      key = neo_location_get(allocator, loc);
      *position = current;
      neo_json_skip_invisible(allocator, file, position);
      if (*position->offset != ':') {
        THROW("Invalid or unexpected token \n  at %ls:%d:%d", file,
              position->line, position->column);
        goto onerror;
      }
      position->column++;
      position->offset++;
      neo_json_skip_invisible(allocator, file, position);
      value = TRY(neo_json_read_variable(allocator, file, position)) {
        goto onerror;
      }
      key[wcslen(key) - 1] = 0;
      neo_variable_set(dict, key + 1, value);
      neo_allocator_free(allocator, key);
      neo_json_skip_invisible(allocator, file, position);
      if (*position->offset == ',') {
        position->offset++;
        position->column++;
        neo_json_skip_invisible(allocator, file, position);
      } else if (*position->offset == '}') {
        break;
      } else {
        THROW("Invalid or unexpected token \n  at %ls:%d:%d", file,
              position->line, position->column);
        goto onerror;
      }
    }
  }
  position->column++;
  position->offset++;
  return dict;
onerror:
  neo_allocator_free(allocator, dict);
  neo_allocator_free(allocator, value);
  neo_allocator_free(allocator, key);
  return NULL;
}

neo_variable_t neo_json_read_array(neo_allocator_t allocator,
                                   const wchar_t *file,
                                   neo_position_t *position) {
  neo_variable_t item = NULL;
  neo_variable_t array = neo_create_variable_array(allocator, NULL, NULL);
  position->offset++;
  position->column++;
  neo_json_skip_invisible(allocator, file, position);
  if (*position->offset != ']') {
    for (;;) {
      item = TRY(neo_json_read_variable(allocator, file, position)) {
        goto onerror;
      }
      neo_variable_push(array, item);
      neo_json_skip_invisible(allocator, file, position);
      if (*position->offset == ',') {
        position->offset++;
        position->column++;
        neo_json_skip_invisible(allocator, file, position);
      } else if (*position->offset == ']') {
        break;
      } else {
        THROW("Invalid or unexpected token \n  at %ls:%d:%d", file,
              position->line, position->column);
        goto onerror;
      }
    }
  }
  position->offset++;
  position->column++;
  return array;
onerror:
  neo_allocator_free(allocator, array);
  neo_allocator_free(allocator, item);
  return NULL;
}

neo_variable_t neo_json_read_variable(neo_allocator_t allocator,
                                      const wchar_t *file,
                                      neo_position_t *position) {
  neo_json_skip_invisible(allocator, file, position);
  if (*position->offset == '{') {
    return neo_json_read_dict(allocator, file, position);
  } else if (*position->offset == '[') {
    return neo_json_read_array(allocator, file, position);
  } else if (*position->offset == '\"') {
    neo_position_t current = *position;
    TRY(neo_json_read_string(allocator, file, &current)) { return NULL; }
    neo_location_t loc = {*position, current, file};
    wchar_t *str = neo_location_get(allocator, loc);
    neo_variable_t res = neo_create_variable_string(allocator, str);
    neo_allocator_free(allocator, str);
    *position = current;
    return res;
  } else if ((*position->offset >= '0' && *position->offset <= '9') ||
             ((*position->offset == '+' || *position->offset == '-') &&
              (*(position->offset + 1) >= '0' &&
               *(position->offset + 1) <= '9'))) {
    neo_position_t current = *position;
    TRY(neo_json_read_number(allocator, file, &current)) { return NULL; }
    neo_location_t loc = {*position, current, file};
    wchar_t *str = neo_location_get(allocator, loc);
    wchar_t *next = NULL;
    double val = wcstod(str, &next);
    neo_variable_t res = neo_create_variable_number(allocator, val);
    neo_allocator_free(allocator, str);
    *position = current;
    return res;
  } else if (strcmp(position->offset, "true") == 0) {
    position->offset += 4;
    position->column += 4;
    return neo_create_variable_boolean(allocator, true);
  } else if (strcmp(position->offset, "false") == 0) {
    position->offset += 5;
    position->column += 5;
    return neo_create_variable_boolean(allocator, false);
  } else if (strcmp(position->offset, "null") == 0) {
    position->offset += 4;
    position->column += 4;
    return neo_create_variable_nil(allocator);
  }
  THROW("Invalid or unexpected token \n  at %ls:%d:%d", file, position->line,
        position->column);
  return NULL;
}

neo_variable_t neo_json_parse(neo_allocator_t allocator, const wchar_t *file,
                              const char *source) {
  neo_position_t position;
  position.offset = source;
  position.column = 0;
  position.line = 0;
  neo_variable_t result =
      TRY(neo_json_read_variable(allocator, file, &position)) {
    return NULL;
  }
  if (*position.offset) {
    neo_allocator_free(allocator, result);
    THROW("Invalid or unexpected token \n  at %ls:%d:%d", file, position.line,
          position.column);
    return NULL;
  }
  return result;
}