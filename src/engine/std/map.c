#include "engine/std/map.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/number.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/scope.h"
#include "engine/std/array.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>


typedef struct _neo_js_map_data_t *neo_js_map_data;
struct _neo_js_map_data_t {
  neo_hash_map_t data;
  neo_list_t keys;
};
static void neo_js_map_data_dispose(neo_allocator_t allocator,
                                    neo_js_map_data data) {
  neo_allocator_free(allocator, data->keys);
  neo_allocator_free(allocator, data->data);
}

static uint32_t neo_js_map_hash(neo_js_handle_t hkey, uint32_t max,
                                neo_js_context_t ctx) {
  neo_js_value_t value = neo_js_handle_get_value(hkey);
  switch (value->type->kind) {
  case NEO_JS_TYPE_NULL:
  case NEO_JS_TYPE_UNDEFINED:
    return 0;
  case NEO_JS_TYPE_NUMBER:
    return (int64_t)neo_js_value_to_number(value)->number % max;
  case NEO_JS_TYPE_STRING:
    return neo_hash_sdb_utf16(neo_js_value_to_string(value)->string, max);
  case NEO_JS_TYPE_BOOLEAN:
    return neo_js_value_to_boolean(value)->boolean ? 1 : 0;
  default:
    return (intptr_t)value % max;
  }
}

static int32_t neo_js_map_cmp(neo_js_handle_t ha, neo_js_handle_t hb,
                              neo_js_context_t ctx) {
  if (neo_js_handle_get_value(ha)->type->kind == NEO_JS_TYPE_NUMBER &&
      neo_js_handle_get_value(hb)->type->kind == NEO_JS_TYPE_NUMBER) {
    neo_js_number_t na = (neo_js_number_t)neo_js_handle_get_value(ha);
    neo_js_number_t nb = (neo_js_number_t)neo_js_handle_get_value(hb);
    if (isnan(na->number) && isnan(nb->number)) {
      return 0;
    }
  }
  neo_js_variable_t a = neo_js_context_create_variable(ctx, ha, NULL);
  neo_js_variable_t b = neo_js_context_create_variable(ctx, hb, NULL);
  if (neo_js_variable_get_type(a) == neo_js_variable_get_type(a)) {
    neo_js_variable_t res = neo_js_context_is_equal(ctx, a, b);
    return neo_js_variable_to_boolean(res)->boolean ? 0 : 1;
  }
  return -1;
}

static neo_js_map_data neo_create_js_map_data(neo_allocator_t allocator) {
  neo_js_map_data data = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_map_data_t), neo_js_map_data_dispose);
  data->keys = neo_create_list(allocator, NULL);
  neo_hash_map_initialize_t initialize = {0};
  initialize.hash = (neo_hash_fn_t)neo_js_map_hash;
  initialize.compare = (neo_compare_fn_t)neo_js_map_cmp;
  initialize.auto_free_key = false;
  initialize.auto_free_value = false;
  data->data = neo_create_hash_map(allocator, &initialize);
  return data;
}

NEO_JS_CFUNCTION(neo_js_map_group_by) {
  neo_js_variable_t inventory = argv[0];
  neo_js_variable_t callback = argv[1];
  neo_js_variable_t map = neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).map_constructor, 0, NULL);
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "iterator"), NULL);
  iterator = neo_js_context_get_field(ctx, inventory, iterator, NULL);
  NEO_JS_TRY_AND_THROW(iterator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, iterator);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              strlen(receiver) + 32,
                                              "%s is not iterable", receiver);
  }
  neo_js_variable_t generator =
      neo_js_context_call(ctx, iterator, inventory, 0, NULL);
  NEO_JS_TRY_AND_THROW(generator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_OBJECT) {
    const char *receiver = neo_js_context_to_error_name(ctx, iterator);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              strlen(receiver) + 32,
                                              "%s is not iterable", receiver);
  }
  neo_js_variable_t next = neo_js_context_get_field(
      ctx, generator, neo_js_context_create_string(ctx, "next"), NULL);
  NEO_JS_TRY_AND_THROW(next);
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, iterator);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              strlen(receiver) + 32,
                                              "%s is not iterable", receiver);
  }
  neo_js_variable_t res = neo_js_context_call(ctx, next, generator, 0, NULL);
  NEO_JS_TRY_AND_THROW(res);
  if (neo_js_variable_get_type(res)->kind < NEO_JS_TYPE_OBJECT) {
    const char *receiver = neo_js_context_to_error_name(ctx, iterator);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              strlen(receiver) + 32,
                                              "%s is not iterable", receiver);
  }
  neo_js_variable_t value = neo_js_context_get_field(
      ctx, res, neo_js_context_create_string(ctx, "value"), NULL);
  NEO_JS_TRY_AND_THROW(value);
  neo_js_variable_t done = neo_js_context_get_field(
      ctx, res, neo_js_context_create_string(ctx, "done"), NULL);
  NEO_JS_TRY_AND_THROW(done);
  done = neo_js_context_to_boolean(ctx, done);
  NEO_JS_TRY_AND_THROW(done);
  for (;;) {
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t key = neo_js_context_call(
        ctx, callback, neo_js_context_create_undefined(ctx), 1, &value);
    neo_js_variable_t item = neo_js_map_get(ctx, map, 1, &key);
    if (neo_js_variable_get_type(item)->kind != NEO_JS_TYPE_ARRAY) {
      item = neo_js_context_create_array(ctx);
      neo_js_variable_t argv[] = {key, item};
      neo_js_map_set(ctx, map, 2, argv);
    }
    neo_js_variable_t len = neo_js_context_get_field(
        ctx, item, neo_js_context_create_string(ctx, "length"), NULL);
    neo_js_context_set_field(ctx, item, len, value, NULL);

    res = neo_js_context_call(ctx, next, generator, 0, NULL);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_get_type(res)->kind < NEO_JS_TYPE_OBJECT) {
      const char *receiver = neo_js_context_to_error_name(ctx, iterator);
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                                strlen(receiver) + 32,
                                                "%s is not iterable", receiver);
    }
    value = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "value"), NULL);
    NEO_JS_TRY_AND_THROW(value);
    done = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "done"), NULL);
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
  }
  return map;
}

NEO_JS_CFUNCTION(neo_js_map_constructor) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_map_data data = neo_create_js_map_data(allocator);
  neo_js_context_set_opaque(ctx, self, "#map", data);
  neo_js_context_set_field(ctx, self, neo_js_context_create_string(ctx, "size"),
                           neo_js_context_create_number(ctx, 0), NULL);
  return self;
}
NEO_JS_CFUNCTION(neo_js_map_clear) {
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  neo_js_context_set_field(ctx, self, neo_js_context_create_string(ctx, "size"),
                           neo_js_context_create_number(ctx, 0), NULL);
  neo_list_clear(data->keys);
  neo_hash_map_clear(data->data);
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_map_delete) {
  neo_js_variable_t key = argv[0];
  neo_js_handle_t hkey = neo_js_variable_get_handle(key);
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  neo_js_handle_t current = neo_hash_map_get(data->data, hkey, ctx, ctx);
  neo_js_handle_t hself = neo_js_variable_get_handle(self);
  if (current) {
    neo_js_handle_t hroot =
        neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx));
    neo_js_handle_add_parent(current, hroot);
    neo_js_handle_remove_parent(current, hself);
    neo_hash_map_delete(data->data, hkey, ctx, ctx);
    neo_list_delete(data->keys, data);
    neo_list_push(data->keys, hkey);
    neo_js_context_set_field(
        ctx, self, neo_js_context_create_string(ctx, "size"),
        neo_js_context_create_number(ctx, neo_list_get_size(data->keys)), NULL);
    return neo_js_context_create_boolean(ctx, true);
  }
  return neo_js_context_create_boolean(ctx, false);
}
NEO_JS_CFUNCTION(neo_js_map_entries) {
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  int64_t idx = 0;
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  for (neo_list_node_t it = neo_list_get_first(data->keys);
       it != neo_list_get_tail(data->keys); it = neo_list_node_next(it)) {
    neo_js_variable_t item = neo_js_context_create_array(ctx);
    neo_js_handle_t hkey = neo_list_node_get(it);
    neo_js_variable_t key = neo_js_context_create_variable(ctx, hkey, NULL);
    neo_js_handle_t hvalue = neo_hash_map_get(data->data, hkey, ctx, ctx);
    neo_js_variable_t value = neo_js_context_create_variable(ctx, hvalue, NULL);
    neo_js_context_set_field(ctx, item, neo_js_context_create_number(ctx, 0),
                             key, NULL);
    neo_js_context_set_field(ctx, item, neo_js_context_create_number(ctx, 1),
                             value, NULL);
    neo_js_context_set_field(ctx, array, neo_js_context_create_number(ctx, idx),
                             item, NULL);
    idx++;
  }
  return neo_js_array_values(ctx, array, 0, NULL);
}

NEO_JS_CFUNCTION(neo_js_map_for_each) {
  neo_js_variable_t callback = argv[0];
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  for (neo_list_node_t it = neo_list_get_first(data->keys);
       it != neo_list_get_tail(data->keys); it = neo_list_node_next(it)) {
    neo_js_handle_t hkey = neo_list_node_get(it);
    neo_js_handle_t hvalue = neo_hash_map_get(data->data, hkey, ctx, ctx);
    neo_js_variable_t key = neo_js_context_create_variable(ctx, hkey, NULL);
    neo_js_variable_t value = neo_js_context_create_variable(ctx, hvalue, NULL);
    neo_js_variable_t argv[] = {key, value, self};
    NEO_JS_TRY_AND_THROW(neo_js_context_call(
        ctx, callback, neo_js_context_create_undefined(ctx), 3, argv));
  }
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_map_get) {
  neo_js_variable_t key = argv[0];
  neo_js_handle_t hkey = neo_js_variable_get_handle(key);
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  neo_js_handle_t current = neo_hash_map_get(data->data, hkey, ctx, ctx);
  if (current) {
    return neo_js_context_create_variable(ctx, current, NULL);
  }
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_map_has) {
  neo_js_variable_t key = argv[0];
  neo_js_handle_t hkey = neo_js_variable_get_handle(key);
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  if (neo_hash_map_has(data->data, hkey, ctx, ctx)) {
    return neo_js_context_create_boolean(ctx, true);
  }
  return neo_js_context_create_boolean(ctx, false);
}
NEO_JS_CFUNCTION(neo_js_map_keys) {
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  int64_t idx = 0;
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  for (neo_list_node_t it = neo_list_get_first(data->keys);
       it != neo_list_get_tail(data->keys); it = neo_list_node_next(it)) {
    neo_js_handle_t hkey = neo_list_node_get(it);
    neo_js_variable_t key = neo_js_context_create_variable(ctx, hkey, NULL);
    neo_js_context_set_field(ctx, array, neo_js_context_create_number(ctx, idx),
                             key, NULL);
    idx++;
  }
  return neo_js_array_values(ctx, array, 0, NULL);
}
NEO_JS_CFUNCTION(neo_js_map_set) {
  neo_js_variable_t key = argv[0];
  neo_js_variable_t value = argv[1];
  neo_js_handle_t hkey = neo_js_variable_get_handle(key);
  neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  neo_js_handle_t current = neo_hash_map_get(data->data, hkey, ctx, ctx);
  neo_js_handle_t hself = neo_js_variable_get_handle(self);
  if (current) {
    neo_js_handle_t hroot =
        neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx));
    neo_js_handle_add_parent(current, hroot);
    neo_js_handle_remove_parent(current, hself);
    neo_hash_map_delete(data->data, hkey, ctx, ctx);
    neo_list_delete(data->keys, data);
  }
  neo_js_handle_add_parent(hkey, hself);
  neo_js_handle_add_parent(hvalue, hself);
  neo_hash_map_set(data->data, hkey, hvalue, ctx, ctx);
  neo_list_push(data->keys, hkey);
  neo_js_context_set_field(
      ctx, self, neo_js_context_create_string(ctx, "size"),
      neo_js_context_create_number(ctx, neo_list_get_size(data->keys)), NULL);
  return self;
}
NEO_JS_CFUNCTION(neo_js_map_values) {
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  int64_t idx = 0;
  neo_js_map_data data = neo_js_context_get_opaque(ctx, self, "#map");
  for (neo_list_node_t it = neo_list_get_first(data->keys);
       it != neo_list_get_tail(data->keys); it = neo_list_node_next(it)) {
    neo_js_handle_t hkey = neo_list_node_get(it);
    neo_js_handle_t hvalue = neo_hash_map_get(data->data, hkey, ctx, ctx);
    neo_js_variable_t value = neo_js_context_create_variable(ctx, hvalue, NULL);
    neo_js_context_set_field(ctx, array, neo_js_context_create_number(ctx, idx),
                             value, NULL);
    idx++;
  }
  return neo_js_array_values(ctx, array, 0, NULL);
}
NEO_JS_CFUNCTION(neo_js_map_species) { return self; }

void neo_js_context_init_std_map(neo_js_context_t ctx) {
  neo_js_variable_t group_by =
      neo_js_context_create_cfunction(ctx, "groupBy", neo_js_map_group_by);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).map_constructor,
                           neo_js_context_create_string(ctx, "groupBy"),
                           group_by, true, false, true);

  neo_js_variable_t species = neo_js_context_create_cfunction(
      ctx, "[Symbol.species]", neo_js_map_species);
  neo_js_context_def_accessor(
      ctx, neo_js_context_get_std(ctx).map_constructor,
      neo_js_context_get_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor,
          neo_js_context_create_string(ctx, "species"), NULL),
      species, NULL, true, false);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).map_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL);
  neo_js_variable_t clear =
      neo_js_context_create_cfunction(ctx, "clear", neo_js_map_clear);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "clear"), clear,
                           true, false, true);
  neo_js_variable_t delete =
      neo_js_context_create_cfunction(ctx, "delete", neo_js_map_delete);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "delete"), delete,
                           true, false, true);
  neo_js_variable_t entries =
      neo_js_context_create_cfunction(ctx, "entries", neo_js_map_entries);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "entries"),
                           entries, true, false, true);
  neo_js_variable_t for_each =
      neo_js_context_create_cfunction(ctx, "forEach", neo_js_map_for_each);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "forEach"),
                           for_each, true, false, true);
  neo_js_variable_t get =
      neo_js_context_create_cfunction(ctx, "get", neo_js_map_get);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "get"), get, true,
                           false, true);
  neo_js_variable_t has =
      neo_js_context_create_cfunction(ctx, "has", neo_js_map_has);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "has"), has, true,
                           false, true);
  neo_js_variable_t keys =
      neo_js_context_create_cfunction(ctx, "keys", neo_js_map_keys);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "keys"), keys,
                           true, false, true);
  neo_js_variable_t set =
      neo_js_context_create_cfunction(ctx, "set", neo_js_map_set);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "set"), set, true,
                           false, true);
  neo_js_variable_t values =
      neo_js_context_create_cfunction(ctx, "values", neo_js_map_values);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "values"), values,
                           true, false, true);
  neo_js_context_def_field(
      ctx, prototype,
      neo_js_context_get_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor,
          neo_js_context_create_string(ctx, "iterator"), NULL),
      entries, true, false, true);
}