#include "core/json.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/map.h"
#include "core/string.h"
#include "core/variable.h"
#include <stdio.h>
#include <string.h>

neo_string_t neo_json_stringify(neo_allocator_t allocator,
                                neo_variable_t variable) {
  switch (neo_variable_get_type(variable)) {
  case NEO_VARIABLE_NIL:
    return neo_create_string(allocator, "null");
  case NEO_VARIABLE_NUMBER: {
    char buf[32];
    sprintf(buf, "%lf", neo_variable_get_number(variable));
    return neo_create_string(allocator, buf);
  }
  case NEO_VARIABLE_INTEGER: {
    char buf[32];
    sprintf(buf, "%ld", neo_variable_get_integer(variable));
    return neo_create_string(allocator, buf);
  } break;
  case NEO_VARIABLE_STRING: {
    const char *str = neo_variable_get_string(variable);
    neo_string_t result = neo_create_string(allocator, "\"");
    neo_string_concat(result, str);
    neo_string_concat(result, "\"");
    return result;
  }
  case NEO_VARIABLE_BOOLEAN:
    return neo_create_string(
        allocator, neo_variable_get_boolean(variable) ? "true" : "false");
  case NEO_VARIABLE_ARRAY: {
    neo_string_t result = neo_create_string(allocator, "[");
    neo_list_t array = neo_variable_get_array(variable);
    for (neo_list_node_t it = neo_list_get_first(array);
         it != neo_list_get_tail(array); it = neo_list_node_next(it)) {
      if (it != neo_list_get_first(array)) {
        neo_string_concat(result, ",");
      }
      neo_variable_t item = neo_list_node_get(it);
      neo_string_t itemjson = neo_json_stringify(allocator, item);
      neo_string_concat(result, neo_string_get(itemjson));
      neo_allocator_free(allocator, itemjson);
    }
    neo_string_concat(result, "]");
    return result;
  }
  case NEO_VARIABLE_DICT: {
    neo_string_t result = neo_create_string(allocator, "{");
    neo_map_t dict = neo_variable_get_dict(variable);
    for (neo_map_node_t it = neo_map_get_first(dict);
         it != neo_map_get_tail(dict); it = neo_map_node_next(it)) {
      if (it != neo_map_get_first(dict)) {
        neo_string_concat(result, ",");
      }
      const char *key = (const char *)neo_map_node_get_key(it);
      neo_variable_t item = (neo_variable_t)neo_map_node_get_value(it);
      neo_string_t k = neo_create_string(allocator, "\"");
      neo_string_concat(k, key);
      neo_string_concat(k, "\"");
      neo_string_concat(result, neo_string_get(k));
      neo_allocator_free(allocator, k);
      neo_string_concat(result, ":");
      neo_string_t itemjson = neo_json_stringify(allocator, item);
      neo_string_concat(result, neo_string_get(itemjson));
      neo_allocator_free(allocator, itemjson);
    }
    neo_string_concat(result, "}");
    return result;
  }
  }
  return NULL;
}