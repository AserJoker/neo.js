#ifndef _H_NEO_CORE_VARIABLE_
#define _H_NEO_CORE_VARIABLE_
#include "core/allocator.h"
#include "core/list.h"
#include "core/map.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct _neo_variable_t *neo_variable_t;

typedef enum _neo_variable_type_t {
  NEO_VARIABLE_NIL,
  NEO_VARIABLE_NUMBER,
  NEO_VARIABLE_INTEGER,
  NEO_VARIABLE_STRING,
  NEO_VARIABLE_BOOLEAN,
  NEO_VARIABLE_ARRAY,
  NEO_VARIABLE_DICT,
} neo_variable_type_t;

typedef neo_variable_t (*neo_serialize_fn_t)(neo_allocator_t allocator,
                                             void *item);

neo_variable_t neo_create_variable_nil(neo_allocator_t allocator);

neo_variable_t neo_create_variable_number(neo_allocator_t allocator,
                                          double value);

neo_variable_t neo_create_variable_integer(neo_allocator_t allocator,
                                           int64_t value);

neo_variable_t neo_create_variable_string(neo_allocator_t allocator,
                                          const wchar_t *value);

neo_variable_t neo_create_variable_boolean(neo_allocator_t allocator,
                                           bool value);

neo_variable_t neo_create_variable_array(neo_allocator_t allocator,
                                         neo_list_t list,
                                         neo_serialize_fn_t serialize);

neo_variable_t neo_create_variable_dict(neo_allocator_t allocator,
                                        neo_map_t map,
                                        neo_serialize_fn_t serialize);

neo_variable_type_t neo_variable_get_type(neo_variable_t variable);

neo_variable_t neo_variable_set_nil(neo_variable_t self);

neo_variable_t neo_variable_set_number(neo_variable_t self, double value);

neo_variable_t neo_variable_set_integer(neo_variable_t self, int64_t value);

neo_variable_t neo_variable_set_string(neo_variable_t self,
                                       const wchar_t *value);

neo_variable_t neo_variable_set_boolean(neo_variable_t self, bool value);

neo_variable_t neo_variable_set_array(neo_variable_t self, neo_list_t list,
                                      neo_serialize_fn_t serialize);

neo_variable_t neo_variable_set_dict(neo_variable_t self, neo_map_t map,
                                     neo_serialize_fn_t serialize);

double neo_variable_get_number(neo_variable_t self);

int64_t neo_variable_get_integer(neo_variable_t self);

wchar_t *neo_variable_get_string(neo_variable_t self);

bool neo_variable_get_boolean(neo_variable_t self);

neo_list_t neo_variable_get_array(neo_variable_t self);

neo_map_t neo_variable_get_dict(neo_variable_t self);

neo_variable_t neo_variable_push(neo_variable_t self, neo_variable_t item);

neo_variable_t neo_variable_set(neo_variable_t self, const wchar_t *key,
                                neo_variable_t item);

#endif