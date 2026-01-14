#include "neo.js/core/any.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/list.h"
#include "neo.js/core/map.h"
#include "neo.js/core/string.h"
#include <string.h>

struct _neo_any_t {
  neo_any_type_t type;
  neo_allocator_t allocator;
  union {
    double number;
    int64_t integer;
    char *string;
    bool boolean;
    neo_list_t array;
    neo_map_t dict;
  };
};

static void neo_any_dispose(neo_allocator_t allocator, neo_any_t variable) {
  if (variable->type == NEO_ANY_ARRAY) {
    neo_allocator_free(allocator, variable->array);
  }
  if (variable->type == NEO_ANY_DICT) {
    neo_allocator_free(allocator, variable->dict);
  }
  if (variable->type == NEO_ANY_STRING) {
    neo_allocator_free(allocator, variable->string);
  }
}

neo_any_t neo_create_any_nil(neo_allocator_t allocator) {
  neo_any_t variable = (neo_any_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_any_t), neo_any_dispose);
  variable->type = NEO_ANY_NIL;
  variable->allocator = allocator;
  return variable;
}

neo_any_t neo_create_any_number(neo_allocator_t allocator, double value) {
  neo_any_t variable = neo_create_any_nil(allocator);
  return neo_any_set_number(variable, value);
}

neo_any_t neo_create_any_integer(neo_allocator_t allocator, int64_t value) {
  neo_any_t variable = neo_create_any_nil(allocator);
  return neo_any_set_integer(variable, value);
}

neo_any_t neo_create_any_string(neo_allocator_t allocator, const char *value) {
  neo_any_t variable = neo_create_any_nil(allocator);
  return neo_any_set_string(variable, value);
}

neo_any_t neo_create_any_boolean(neo_allocator_t allocator, bool value) {
  neo_any_t variable = neo_create_any_nil(allocator);
  return neo_any_set_boolean(variable, value);
}

neo_any_t neo_create_any_array(neo_allocator_t allocator, neo_list_t list,
                               neo_serialize_fn_t serizalize) {
  neo_any_t variable = neo_create_any_nil(allocator);
  return neo_any_set_array(variable, list, serizalize);
}

neo_any_t neo_create_any_dict(neo_allocator_t allocator, neo_map_t map,
                              neo_serialize_fn_t serizalize) {
  neo_any_t variable = neo_create_any_nil(allocator);
  return neo_any_set_dict(variable, map, serizalize);
}

neo_any_type_t neo_any_get_type(neo_any_t variable) { return variable->type; }

neo_any_t neo_any_set_nil(neo_any_t self) {
  neo_any_dispose(self->allocator, self);
  self->type = NEO_ANY_NIL;
  return self;
}

neo_any_t neo_any_set_number(neo_any_t self, double value) {
  neo_any_dispose(self->allocator, self);
  self->type = NEO_ANY_NUMBER;
  self->number = value;
  return self;
}

neo_any_t neo_any_set_integer(neo_any_t self, int64_t value) {
  neo_any_dispose(self->allocator, self);
  self->type = NEO_ANY_INTEGER;
  self->integer = value;
  return self;
}

neo_any_t neo_any_set_string(neo_any_t self, const char *value) {
  neo_any_dispose(self->allocator, self);
  self->type = NEO_ANY_STRING;
  self->string = neo_create_string(self->allocator, value);
  return self;
}

neo_any_t neo_any_set_boolean(neo_any_t self, bool value) {
  neo_any_dispose(self->allocator, self);
  self->type = NEO_ANY_BOOLEAN;
  self->boolean = value;
  return self;
}

neo_any_t neo_any_set_array(neo_any_t self, neo_list_t list,
                            neo_serialize_fn_t serialize) {
  neo_any_dispose(self->allocator, self);
  self->type = NEO_ANY_ARRAY;
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

neo_any_t neo_any_set_dict(neo_any_t self, neo_map_t map,
                           neo_serialize_fn_t serialize) {
  neo_any_dispose(self->allocator, self);
  self->type = NEO_ANY_DICT;
  neo_map_initialize_t initialize = {true, true, (neo_compare_fn_t)strcmp};
  self->dict = neo_create_map(self->allocator, &initialize);
  if (map && neo_map_get_size(map)) {
    for (neo_map_node_t it = neo_map_get_first(map);
         it != neo_map_get_tail(map); it = neo_map_node_next(it)) {
      const char *key = neo_map_node_get_key(it);
      neo_any_set(self, key,
                  serialize(self->allocator, neo_map_node_get_value(it)));
    }
  }
  return self;
}

double neo_any_get_number(neo_any_t self) { return self->number; }

int64_t neo_any_get_integer(neo_any_t self) { return self->integer; }

char *neo_any_get_string(neo_any_t self) { return self->string; }

bool neo_any_get_boolean(neo_any_t self) { return self->boolean; }

neo_list_t neo_any_get_array(neo_any_t self) { return self->array; }

neo_map_t neo_any_get_dict(neo_any_t self) { return self->dict; }

neo_any_t neo_any_push(neo_any_t self, neo_any_t item) {

  neo_list_push(self->array, item);
  return self;
}

neo_any_t neo_any_set(neo_any_t self, const char *key, neo_any_t item) {
  char *buf = neo_create_string(self->allocator, key);
  neo_map_set(self->dict, buf, item);
  return self;
}