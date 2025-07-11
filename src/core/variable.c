#include "core/variable.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/map.h"
#include "core/string.h"
#include <string.h>
#include <wchar.h>

struct _neo_variable_t {
  neo_variable_type_t type;
  neo_allocator_t allocator;
  union {
    double number;
    int64_t integer;
    wchar_t *string;
    bool boolean;
    neo_list_t array;
    neo_map_t dict;
  };
};

static void neo_variable_dispose(neo_allocator_t allocator,
                                 neo_variable_t variable) {
  if (variable->type == NEO_VARIABLE_ARRAY) {
    neo_allocator_free(allocator, variable->array);
  }
  if (variable->type == NEO_VARIABLE_DICT) {
    neo_allocator_free(allocator, variable->dict);
  }
  if (variable->type == NEO_VARIABLE_STRING) {
    neo_allocator_free(allocator, variable->string);
  }
}

neo_variable_t neo_create_variable_nil(neo_allocator_t allocator) {
  neo_variable_t variable = (neo_variable_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_variable_t), neo_variable_dispose);
  variable->type = NEO_VARIABLE_NIL;
  variable->allocator = allocator;
  return variable;
}

neo_variable_t neo_create_variable_number(neo_allocator_t allocator,
                                          double value) {
  neo_variable_t variable = neo_create_variable_nil(allocator);
  return neo_variable_set_number(variable, value);
}

neo_variable_t neo_create_variable_integer(neo_allocator_t allocator,
                                           int64_t value) {
  neo_variable_t variable = neo_create_variable_nil(allocator);
  return neo_variable_set_integer(variable, value);
}

neo_variable_t neo_create_variable_string(neo_allocator_t allocator,
                                          const wchar_t *value) {
  neo_variable_t variable = neo_create_variable_nil(allocator);
  return neo_variable_set_string(variable, value);
}

neo_variable_t neo_create_variable_boolean(neo_allocator_t allocator,
                                           bool value) {
  neo_variable_t variable = neo_create_variable_nil(allocator);
  return neo_variable_set_boolean(variable, value);
}

neo_variable_t neo_create_variable_array(neo_allocator_t allocator,
                                         neo_list_t list,
                                         neo_serialize_fn_t serizalize) {
  neo_variable_t variable = neo_create_variable_nil(allocator);
  return neo_variable_set_array(variable, list, serizalize);
}

neo_variable_t neo_create_variable_dict(neo_allocator_t allocator,
                                        neo_map_t map,
                                        neo_serialize_fn_t serizalize) {
  neo_variable_t variable = neo_create_variable_nil(allocator);
  return neo_variable_set_dict(variable, map, serizalize);
}

neo_variable_type_t neo_variable_get_type(neo_variable_t variable) {
  return variable->type;
}

neo_variable_t neo_variable_set_nil(neo_variable_t self) {
  neo_variable_dispose(self->allocator, self);
  self->type = NEO_VARIABLE_NIL;
  return self;
}

neo_variable_t neo_variable_set_number(neo_variable_t self, double value) {
  neo_variable_dispose(self->allocator, self);
  self->type = NEO_VARIABLE_NUMBER;
  self->number = value;
  return self;
}

neo_variable_t neo_variable_set_integer(neo_variable_t self, int64_t value) {
  neo_variable_dispose(self->allocator, self);
  self->type = NEO_VARIABLE_INTEGER;
  self->integer = value;
  return self;
}

neo_variable_t neo_variable_set_string(neo_variable_t self,
                                       const wchar_t *value) {
  neo_variable_dispose(self->allocator, self);
  self->type = NEO_VARIABLE_STRING;
  self->string = neo_create_wstring(self->allocator, value);
  return self;
}

neo_variable_t neo_variable_set_boolean(neo_variable_t self, bool value) {
  neo_variable_dispose(self->allocator, self);
  self->type = NEO_VARIABLE_BOOLEAN;
  self->boolean = value;
  return self;
}

neo_variable_t neo_variable_set_array(neo_variable_t self, neo_list_t list,
                                      neo_serialize_fn_t serialize) {
  neo_variable_dispose(self->allocator, self);
  self->type = NEO_VARIABLE_ARRAY;
  neo_list_initialize_t initialize = {true};
  self->array = neo_create_list(self->allocator, &initialize);
  if (list && neo_list_get_size(list)) {
    for (neo_list_node_t it = neo_list_get_first(list);
         it != neo_list_get_tail(list); it = neo_list_node_next(it)) {
      neo_list_push(self->array,
                    serialize(self->allocator, neo_list_node_get(it)));
    }
  }
  return self;
}

neo_variable_t neo_variable_set_dict(neo_variable_t self, neo_map_t map,
                                     neo_serialize_fn_t serialize) {
  neo_variable_dispose(self->allocator, self);
  self->type = NEO_VARIABLE_DICT;
  neo_map_initialize_t initialize = {true, true, (neo_compare_fn_t)strcmp};
  self->dict = neo_create_map(self->allocator, &initialize);
  if (map && neo_map_get_size(map)) {
    for (neo_map_node_t it = neo_map_get_first(map);
         it != neo_map_get_tail(map); it = neo_map_node_next(it)) {
      const wchar_t *key = neo_map_node_get_key(it);
      neo_variable_set(self, key,
                       serialize(self->allocator, neo_map_node_get_value(it)));
    }
  }
  return self;
}

double neo_variable_get_number(neo_variable_t self) { return self->number; }

int64_t neo_variable_get_integer(neo_variable_t self) { return self->integer; }

wchar_t *neo_variable_get_string(neo_variable_t self) { return self->string; }

bool neo_variable_get_boolean(neo_variable_t self) { return self->boolean; }

neo_list_t neo_variable_get_array(neo_variable_t self) { return self->array; }

neo_map_t neo_variable_get_dict(neo_variable_t self) { return self->dict; }

neo_variable_t neo_variable_push(neo_variable_t self, neo_variable_t item) {

  neo_list_push(self->array, item);
  return self;
}

neo_variable_t neo_variable_set(neo_variable_t self, const wchar_t *key,
                                neo_variable_t item) {
  wchar_t *buf = neo_create_wstring(self->allocator, key);
  neo_map_set(self->dict, buf, item, NULL);
  return self;
}