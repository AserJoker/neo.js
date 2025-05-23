#include "core/json.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/map.h"
#include "core/variable.h"
#include <stdio.h>
#include <string.h>

static char *neo_string_concat(neo_allocator_t allocator, char *src,
                               size_t *max, const char *str) {
  char *result = src;
  size_t len = strlen(str);
  size_t base = strlen(src);
  if (base + len > *max) {
    while (base + len > *max) {
      *max += 128;
    }
    result = neo_allocator_alloc(allocator, *max, NULL);
    strcpy(result, src);
    result[base] = 0;
    neo_allocator_free(allocator, src);
  }
  strcpy(result + base, str);
  result[base + len] = 0;
  return result;
}

char *neo_json_stringify(neo_allocator_t allocator, neo_variable_t variable) {
  char *result = neo_allocator_alloc(allocator, 128, NULL);
  result[0] = 0;
  size_t max = 0;
  switch (neo_variable_get_type(variable)) {
  case NEO_VARIABLE_NIL:
    result = neo_string_concat(allocator, result, &max, "null");
    break;
  case NEO_VARIABLE_NUMBER: {
    char tmp[16];
    sprintf(tmp, "%lf", neo_variable_get_number(variable));
    result = neo_string_concat(allocator, result, &max, tmp);
    break;
  }
  case NEO_VARIABLE_INTEGER: {
    char tmp[16];
    sprintf(tmp, "%ld", neo_variable_get_integer(variable));
    result = neo_string_concat(allocator, result, &max, tmp);
    break;
  }
  case NEO_VARIABLE_STRING: {
    const char *src = neo_variable_get_string(variable);
    size_t len = strlen(src);
    char *buf = neo_allocator_alloc(allocator, len * 2, NULL);
    char *dst = buf;
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
    result = neo_string_concat(allocator, result, &max, "\"");
    result = neo_string_concat(allocator, result, &max, buf);
    result = neo_string_concat(allocator, result, &max, "\"");
    neo_allocator_free(allocator, buf);
  } break;
  case NEO_VARIABLE_BOOLEAN:
    result = neo_string_concat(allocator, result, &max,
                               neo_variable_get_boolean(variable) ? "true"
                                                                  : "false");
    break;
  case NEO_VARIABLE_ARRAY: {
    neo_list_t array = neo_variable_get_array(variable);
    result = neo_string_concat(allocator, result, &max, "[");
    for (neo_list_node_t it = neo_list_get_first(array);
         it != neo_list_get_tail(array); it = neo_list_node_next(it)) {
      neo_variable_t item = neo_list_node_get(it);
      if (it != neo_list_get_first(array)) {
        result = neo_string_concat(allocator, result, &max, ",");
      }
      char *itemjson = neo_json_stringify(allocator, item);
      result = neo_string_concat(allocator, result, &max, itemjson);
      neo_allocator_free(allocator, itemjson);
    }
    result = neo_string_concat(allocator, result, &max, "]");
  } break;
  case NEO_VARIABLE_DICT: {
    neo_map_t dict = neo_variable_get_dict(variable);
    result = neo_string_concat(allocator, result, &max, "{");
    for (neo_map_node_t it = neo_map_get_first(dict);
         it != neo_map_get_tail(dict); it = neo_map_node_next(it)) {
      char *key = neo_map_node_get_key(it);
      neo_variable_t item = neo_map_node_get_value(it);
      if (it != neo_map_get_first(dict)) {
        result = neo_string_concat(allocator, result, &max, ",");
      }
      result = neo_string_concat(allocator, result, &max, "\"");
      result = neo_string_concat(allocator, result, &max, key);
      result = neo_string_concat(allocator, result, &max, "\"");
      result = neo_string_concat(allocator, result, &max, ":");
      char *itemjson = neo_json_stringify(allocator, item);
      result = neo_string_concat(allocator, result, &max, itemjson);
      neo_allocator_free(allocator, itemjson);
    }
    result = neo_string_concat(allocator, result, &max, "}");
  } break;
  }
  return result;
}