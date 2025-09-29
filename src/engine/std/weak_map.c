#include "engine/std/weak_map.h"
#include "core/allocator.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/number.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>

static uint32_t neo_js_weak_map_hash(neo_js_handle_t hkey, uint32_t max,
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

static int32_t neo_js_weak_map_cmp(neo_js_handle_t ha, neo_js_handle_t hb,
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

NEO_JS_CFUNCTION(neo_js_weak_map_constructor) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_hash_map_initialize_t initialize = {0};
  initialize.hash = (neo_hash_fn_t)neo_js_weak_map_hash;
  initialize.compare = (neo_compare_fn_t)neo_js_weak_map_cmp;
  initialize.auto_free_key = false;
  initialize.auto_free_value = false;
  neo_hash_map_t data = neo_create_hash_map(allocator, &initialize);
  neo_js_context_set_opaque(ctx, self, "#data", data);
  return self;
}
NEO_JS_CFUNCTION(neo_js_weak_map_get) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#data");
  neo_js_variable_t key = NULL;
  if (argc) {
    key = argv[0];
  }
  if (!key || neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_OBJECT &&
                  neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Invalid value used as weak map key");
  }
  neo_js_handle_t hself = neo_js_variable_get_handle(self);
  neo_js_handle_t hkey = neo_js_variable_get_handle(key);
  neo_list_t weak_chidlren = neo_js_handle_get_week_children(hself);
  if (!neo_list_find(weak_chidlren, hkey)) {
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_handle_t hvalue = neo_hash_map_get(data, hkey, ctx, ctx);
  return neo_js_context_create_variable(ctx, hvalue, NULL);
}
NEO_JS_CFUNCTION(neo_js_weak_map_set) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#data");
  neo_js_variable_t key = NULL;
  neo_js_variable_t value = NULL;
  if (argc) {
    key = argv[0];
  }
  if (!key || neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_OBJECT &&
                  neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Invalid value used as weak map key");
  }
  if (argc > 1) {
    value = argv[1];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_handle_t hself = neo_js_variable_get_handle(self);
  neo_js_handle_t hkey = neo_js_variable_get_handle(key);
  neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
  neo_hash_map_set(data, hkey, hvalue, ctx, ctx);
  neo_js_handle_add_parent(hvalue, hkey);
  neo_js_handle_add_weak_parent(hkey, hself);
  return self;
}

NEO_JS_CFUNCTION(neo_js_weak_map_has) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#data");
  neo_js_variable_t key = NULL;
  if (argc) {
    key = argv[0];
  }
  if (!key || neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_OBJECT &&
                  neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Invalid value used as weak map key");
  }
  neo_js_handle_t hself = neo_js_variable_get_handle(self);
  neo_js_handle_t hkey = neo_js_variable_get_handle(key);
  neo_list_t weak_chidlren = neo_js_handle_get_week_children(hself);
  if (!neo_list_find(weak_chidlren, hkey)) {
    return neo_js_context_create_boolean(ctx, false);
  }
  return neo_js_context_create_boolean(ctx, true);
}

NEO_JS_CFUNCTION(neo_js_weak_map_delete) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#data");
  neo_js_variable_t key = NULL;
  if (argc) {
    key = argv[0];
  }
  if (!key || neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_OBJECT &&
                  neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Invalid value used as weak map key");
  }
  neo_js_handle_t hself = neo_js_variable_get_handle(self);
  neo_js_handle_t hkey = neo_js_variable_get_handle(key);
  neo_list_t weak_chidlren = neo_js_handle_get_week_children(hself);
  if (!neo_list_find(weak_chidlren, hkey)) {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_handle_t hvalue = neo_hash_map_get(data, hkey, ctx, ctx);
  neo_js_handle_remove_parent(hvalue, hkey);
  neo_js_handle_remove_weak_parent(hkey, hself);
  neo_hash_map_delete(data, hkey, ctx, ctx);
  return neo_js_context_create_boolean(ctx, true);
}

void neo_js_context_init_std_weak_map(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_string_field(
      ctx, neo_js_context_get_std(ctx).weak_map_constructor, "prototype");
  NEO_JS_SET_METHOD(ctx, prototype, "get", neo_js_weak_map_get);
  NEO_JS_SET_METHOD(ctx, prototype, "set", neo_js_weak_map_set);
  NEO_JS_SET_METHOD(ctx, prototype, "has", neo_js_weak_map_has);
  NEO_JS_SET_METHOD(ctx, prototype, "delete", neo_js_weak_map_delete);
}