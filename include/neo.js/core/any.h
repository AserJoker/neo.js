#ifndef _H_NEO_CORE_ANY_
#define _H_NEO_CORE_ANY_
#ifdef __cplusplus
extern "C" {
#endif
#include "neo.js/core/allocator.h"
#include "neo.js/core/list.h"
#include "neo.js/core/map.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct _neo_any_t *neo_any_t;

typedef enum _neo_any_type_t {
  NEO_ANY_NIL,
  NEO_ANY_NUMBER,
  NEO_ANY_INTEGER,
  NEO_ANY_STRING,
  NEO_ANY_BOOLEAN,
  NEO_ANY_ARRAY,
  NEO_ANY_DICT,
} neo_any_type_t;

typedef neo_any_t (*neo_serialize_fn_t)(neo_allocator_t allocator, void *item);

neo_any_t neo_create_any_nil(neo_allocator_t allocator);

neo_any_t neo_create_any_number(neo_allocator_t allocator, double value);

neo_any_t neo_create_any_integer(neo_allocator_t allocator, int64_t value);

neo_any_t neo_create_any_string(neo_allocator_t allocator, const char *value);

neo_any_t neo_create_any_boolean(neo_allocator_t allocator, bool value);

neo_any_t neo_create_any_array(neo_allocator_t allocator, neo_list_t list,
                               neo_serialize_fn_t serialize);

neo_any_t neo_create_any_dict(neo_allocator_t allocator, neo_map_t map,
                              neo_serialize_fn_t serialize);

neo_any_type_t neo_any_get_type(neo_any_t variable);

neo_any_t neo_any_set_nil(neo_any_t self);

neo_any_t neo_any_set_number(neo_any_t self, double value);

neo_any_t neo_any_set_integer(neo_any_t self, int64_t value);

neo_any_t neo_any_set_string(neo_any_t self, const char *value);

neo_any_t neo_any_set_boolean(neo_any_t self, bool value);

neo_any_t neo_any_set_array(neo_any_t self, neo_list_t list,
                            neo_serialize_fn_t serialize);

neo_any_t neo_any_set_dict(neo_any_t self, neo_map_t map,
                           neo_serialize_fn_t serialize);

double neo_any_get_number(neo_any_t self);

int64_t neo_any_get_integer(neo_any_t self);

char *neo_any_get_string(neo_any_t self);

bool neo_any_get_boolean(neo_any_t self);

neo_list_t neo_any_get_array(neo_any_t self);

neo_map_t neo_any_get_dict(neo_any_t self);

neo_any_t neo_any_push(neo_any_t self, neo_any_t item);

neo_any_t neo_any_set(neo_any_t self, const char *key, neo_any_t item);
#ifdef __cplusplus
}
#endif
#endif