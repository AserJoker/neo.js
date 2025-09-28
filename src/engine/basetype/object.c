#include "engine/basetype/object.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/number.h"
#include "engine/basetype/ref.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

static const char *neo_js_object_typeof(neo_js_context_t ctx,
                                        neo_js_variable_t variable) {
  return "object";
}

static neo_js_variable_t neo_js_object_to_string(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_js_variable_t to_primitive = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "toPrimitive"), NULL);
  neo_js_variable_t primitive = NULL;
  if (neo_js_variable_get_type(to_primitive)->kind >= NEO_JS_TYPE_CFUNCTION) {
    neo_js_variable_t hint = neo_js_context_create_string(ctx, "string");
    primitive = neo_js_context_call(ctx, to_primitive, self, 1, &hint);
    if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
      return primitive;
    }
  }
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t value_of = neo_js_context_get_field(
        ctx, self, neo_js_context_create_string(ctx, "valueOf"), NULL);
    if (neo_js_variable_get_type(value_of)->kind >= NEO_JS_TYPE_CFUNCTION) {
      primitive = neo_js_context_call(ctx, value_of, self, 0, NULL);
      if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
        return primitive;
      }
    }
  }
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t to_string = neo_js_context_get_field(
        ctx, self, neo_js_context_create_string(ctx, "toString"), NULL);
    if (neo_js_variable_get_type(to_string)->kind >= NEO_JS_TYPE_CFUNCTION) {
      primitive = neo_js_context_call(ctx, to_string, self, 0, NULL);
      if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
        return primitive;
      }
    }
  }
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind >= NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Cannot convert object to primitive value");
  }
  return neo_js_context_to_string(ctx, primitive);
}

static neo_js_variable_t neo_js_object_to_boolean(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  return neo_js_context_create_boolean(ctx, true);
}
static neo_js_variable_t neo_js_object_to_number(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_js_variable_t to_primitive = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "toPrimitive"), NULL);
  neo_js_variable_t primitive = NULL;
  if (neo_js_variable_get_type(to_primitive)->kind == NEO_JS_TYPE_CFUNCTION) {
    neo_js_variable_t hint = neo_js_context_create_string(ctx, "number");
    primitive = neo_js_context_call(ctx, to_primitive, self, 1, &hint);
    if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
      return primitive;
    }
  }
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t value_of = neo_js_context_get_field(
        ctx, self, neo_js_context_create_string(ctx, "valueOf"), NULL);
    if (neo_js_variable_get_type(value_of)->kind == NEO_JS_TYPE_CFUNCTION) {
      primitive = neo_js_context_call(ctx, value_of, self, 0, NULL);

      if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
        return primitive;
      }
    }
  }
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t to_string = neo_js_context_get_field(
        ctx, self, neo_js_context_create_string(ctx, "toString"), NULL);
    if (neo_js_variable_get_type(to_string)->kind == NEO_JS_TYPE_CFUNCTION) {
      primitive = neo_js_context_call(ctx, to_string, self, 0, NULL);
      if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
        return primitive;
      }
    }
  }
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind != NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Cannot convert object to primitive value");
  }
  return neo_js_context_to_number(ctx, primitive);
}
static neo_js_variable_t neo_js_object_to_primitive(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    const char *type) {
  neo_js_variable_t to_primitive = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "toPrimitive"), NULL);
  neo_js_variable_t primitive = NULL;
  if (neo_js_variable_get_type(to_primitive)->kind == NEO_JS_TYPE_CFUNCTION) {
    neo_js_variable_t hint = neo_js_context_create_string(ctx, type);
    primitive = neo_js_context_call(ctx, to_primitive, self, 1, &hint);
    if (neo_js_variable_get_type(primitive)->kind < NEO_JS_TYPE_OBJECT) {
      return primitive;
    }
    if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
      return primitive;
    }
  }
  neo_js_variable_t value_of = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "valueOf"), NULL);
  if (neo_js_variable_get_type(value_of)->kind == NEO_JS_TYPE_CFUNCTION) {
    primitive = neo_js_context_call(ctx, value_of, self, 0, NULL);
    if (neo_js_variable_get_type(primitive)->kind < NEO_JS_TYPE_OBJECT) {
      return primitive;
    }
    if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
      return primitive;
    }
  }
  neo_js_variable_t to_string = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "toString"), NULL);
  if (neo_js_variable_get_type(to_string)->kind == NEO_JS_TYPE_CFUNCTION) {
    primitive = neo_js_context_call(ctx, to_string, self, 0, NULL);
    if (neo_js_variable_get_type(primitive)->kind < NEO_JS_TYPE_OBJECT) {
      return primitive;
    }
    if (neo_js_variable_get_type(primitive)->kind == NEO_JS_TYPE_ERROR) {
      return primitive;
    }
  }
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind >= NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Cannot convert object to primitive value");
  }
  return neo_js_context_create_simple_error(
      ctx, NEO_JS_ERROR_TYPE, 0, "Cannot convert object to primitive value");
}
static neo_js_variable_t neo_js_object_to_object(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  return self;
}

neo_js_object_property_t
neo_create_js_object_property(neo_allocator_t allocator) {
  neo_js_object_property_t property = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_object_property_t), NULL);
  property->configurable = false;
  property->enumerable = false;
  property->writable = false;
  property->value = NULL;
  property->get = NULL;
  property->set = NULL;
  return property;
}
neo_js_object_private_t
neo_create_js_object_private(neo_allocator_t allocator) {
  neo_js_object_private_t property = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_object_private_t), NULL);
  property->value = NULL;
  property->get = NULL;
  property->set = NULL;
  return property;
}

neo_js_object_property_t
neo_js_object_get_own_property(neo_js_context_t ctx, neo_js_variable_t self,
                               neo_js_variable_t field) {
  if (neo_js_variable_get_type(field)->kind != NEO_JS_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_t object =
      neo_js_value_to_object(neo_js_variable_get_value(self));
  neo_js_object_property_t property = NULL;
  neo_js_handle_t hfield = neo_js_variable_get_handle(field);
  property = neo_hash_map_get(object->properties, hfield, ctx, ctx);
  return property;
}

static neo_list_t neo_js_object_sort_keys(neo_allocator_t allocator,
                                          neo_list_t keys) {
  if (neo_list_get_size(keys) <= 1) {
    return keys;
  }
  neo_list_t left = neo_create_list(allocator, NULL);
  neo_list_t right = neo_create_list(allocator, NULL);
  neo_list_node_t it = neo_list_get_first(keys);
  neo_js_variable_t pin = neo_list_node_get(it);
  char *spin = neo_js_string_to_cstring(allocator, pin);
  it = neo_list_node_next(it);
  while (it != neo_list_get_tail(keys)) {
    neo_js_variable_t key = neo_list_node_get(it);
    char *s = neo_js_string_to_cstring(allocator, key);
    if (strcmp(s, spin) < 0) {
      neo_list_push(left, key);
    } else {
      neo_list_push(right, key);
    }
    neo_allocator_free(allocator, s);
    it = neo_list_node_next(it);
  }
  left = neo_js_object_sort_keys(allocator, left);
  right = neo_js_object_sort_keys(allocator, right);
  neo_list_push(left, pin);
  for (neo_list_node_t it = neo_list_get_first(right);
       it != neo_list_get_tail(right); it = neo_list_node_next(it)) {
    neo_list_push(left, neo_list_node_get(it));
  }
  neo_allocator_free(allocator, spin);
  neo_allocator_free(allocator, right);
  neo_allocator_free(allocator, keys);
  return left;
}

neo_list_t neo_js_object_get_own_keys(neo_js_context_t ctx,
                                      neo_js_variable_t self) {
  neo_js_object_t object =
      neo_js_value_to_object(neo_js_variable_get_value(self));
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_list_t keys = neo_create_list(allocator, NULL);
  neo_list_t num_keys = neo_create_list(allocator, NULL);
  for (neo_list_node_t it = neo_list_get_first(object->keys);
       it != neo_list_get_tail(object->keys); it = neo_list_node_next(it)) {
    neo_js_handle_t hkey = neo_list_node_get(it);
    neo_js_variable_t key = neo_js_context_create_variable(ctx, hkey, NULL);
    neo_js_variable_t nkey = neo_js_context_to_number(ctx, key);
    neo_js_number_t num = neo_js_variable_to_number(nkey);
    if (!isnan(num->number) && num->number >= 0) {
      neo_list_push(num_keys, key);
    } else {
      neo_list_push(keys, key);
    }
  }
  num_keys = neo_js_object_sort_keys(allocator, num_keys);
  for (neo_list_node_t it = neo_list_get_first(keys);
       it != neo_list_get_tail(keys); it = neo_list_node_next(it)) {
    neo_js_variable_t key = neo_list_node_get(it);
    neo_list_push(num_keys, key);
  }
  neo_allocator_free(allocator, keys);
  return num_keys;
}

static void neo_js_object_get_keys_list(neo_js_context_t ctx,
                                        neo_js_object_t proto,
                                        neo_list_t num_keys, neo_list_t keys) {
  if (!proto) {
    return;
  }
  if (proto->prototype) {
    neo_js_object_t obj =
        neo_js_value_to_object(neo_js_handle_get_value(proto->prototype));
    neo_js_object_get_keys_list(ctx, obj, num_keys, keys);
  }
  for (neo_list_node_t it = neo_list_get_first(proto->keys);
       it != neo_list_get_tail(proto->keys); it = neo_list_node_next(it)) {
    neo_js_handle_t hkey = neo_list_node_get(it);
    neo_js_variable_t key = neo_js_context_create_variable(ctx, hkey, NULL);
    const char *skey = neo_js_context_to_cstring(ctx, key);
    neo_js_variable_t nkey = neo_js_context_to_number(ctx, key);
    neo_js_number_t num = neo_js_variable_to_number(nkey);
    if (!isnan(num->number) && num->number >= 0) {
      neo_list_node_t it2 = NULL;
      for (it2 = neo_list_get_first(num_keys);
           it2 != neo_list_get_tail(num_keys); it2 = neo_list_node_next(it2)) {
        neo_js_variable_t current = neo_list_node_get(it2);
        if (strcmp(neo_js_context_to_cstring(ctx, current), skey) == 0) {
          break;
        }
      }
      if (it2 == neo_list_get_tail(num_keys)) {
        neo_list_push(num_keys, key);
      }
    } else {
      neo_list_node_t it2 = NULL;
      for (it2 = neo_list_get_first(keys); it2 != neo_list_get_tail(keys);
           it2 = neo_list_node_next(it2)) {
        neo_js_variable_t current = neo_list_node_get(it2);
        if (strcmp(neo_js_context_to_cstring(ctx, current), skey) == 0) {
          break;
        }
      }
      if (it2 == neo_list_get_tail(keys)) {
        neo_list_push(keys, key);
      }
    }
  }
}

neo_list_t neo_js_object_get_keys(neo_js_context_t ctx,
                                  neo_js_variable_t self) {
  neo_js_object_t object =
      neo_js_value_to_object(neo_js_variable_get_value(self));
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_list_t keys = neo_create_list(allocator, NULL);
  neo_list_t num_keys = neo_create_list(allocator, NULL);
  neo_js_object_get_keys_list(ctx, object, num_keys, keys);
  num_keys = neo_js_object_sort_keys(allocator, num_keys);
  for (neo_list_node_t it = neo_list_get_first(keys);
       it != neo_list_get_tail(keys); it = neo_list_node_next(it)) {
    neo_js_variable_t key = neo_list_node_get(it);
    neo_list_push(num_keys, key);
  }
  neo_allocator_free(allocator, keys);
  return num_keys;
}

neo_list_t neo_js_object_get_own_symbol_keys(neo_js_context_t ctx,
                                             neo_js_variable_t self) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_object_t obj = neo_js_variable_to_object(self);
  neo_list_t symbol_keys = neo_create_list(allocator, NULL);
  for (neo_list_node_t it = neo_list_get_first(obj->symbol_keys);
       it != neo_list_get_tail(obj->symbol_keys); it = neo_list_node_next(it)) {
    neo_js_handle_t hkey = neo_list_node_get(it);
    neo_list_push(symbol_keys, neo_js_context_create_variable(ctx, hkey, NULL));
  }
  return symbol_keys;
}

neo_js_object_property_t neo_js_object_get_property(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    neo_js_variable_t field) {
  if (neo_js_variable_get_type(field)->kind != NEO_JS_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_t object =
      neo_js_value_to_object(neo_js_variable_get_value(self));
  neo_js_object_property_t property = NULL;
  neo_js_handle_t hfield = neo_js_variable_get_handle(field);
  while (!property && object) {
    property = neo_hash_map_get(object->properties, hfield, ctx, ctx);
    if (property) {
      break;
    }
    object = neo_js_value_to_object(neo_js_handle_get_value(object->prototype));
  }
  return property;
}

neo_js_variable_t neo_js_object_get_constructor(neo_js_context_t ctx,
                                                neo_js_variable_t self) {
  neo_js_object_t obj = neo_js_variable_to_object(self);
  return neo_js_context_create_variable(ctx, obj->constructor, NULL);
}

neo_js_variable_t neo_js_object_get_prototype(neo_js_context_t ctx,
                                              neo_js_variable_t self) {
  if (neo_js_variable_get_type(self)->kind >= NEO_JS_TYPE_OBJECT &&
      neo_js_context_has_internal(ctx, self, "[[target]]") &&
      neo_js_context_has_internal(ctx, self, "[[handler]]")) {
    neo_js_variable_t target =
        neo_js_context_get_internal(ctx, self, "[[target]]");
    neo_js_variable_t handler =
        neo_js_context_get_internal(ctx, self, "[[handler]]");
    if (neo_js_context_has_field(
            ctx, handler,
            neo_js_context_create_string(ctx, "getPrototypeOf"))) {
      neo_js_variable_t getPrototypeOf = neo_js_context_get_field(
          ctx, handler, neo_js_context_create_string(ctx, "getPrototypeOf"),
          NULL);
      NEO_JS_TRY_AND_THROW(getPrototypeOf);
      neo_js_variable_t args[] = {target};
      return neo_js_context_call(ctx, getPrototypeOf,
                                 neo_js_context_create_undefined(ctx), 1, args);
    }
  }
  neo_js_object_t obj = neo_js_variable_to_object(self);
  return neo_js_context_create_variable(ctx, obj->prototype, NULL);
}

static neo_js_variable_t neo_js_object_get_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field,
                                                 neo_js_variable_t receiver) {
  neo_js_object_property_t property =
      neo_js_object_get_property(ctx, self, field);
  if (!property) {
    return neo_js_context_create_undefined(ctx);
  }
  if (property->get) {
    neo_js_variable_t getter =
        neo_js_context_create_variable(ctx, property->get, NULL);
    return neo_js_context_call(ctx, getter, receiver ? receiver : self, 0,
                               NULL);
  } else if (property->set) {
    return neo_js_context_create_undefined(ctx);
  } else {
    return neo_js_context_create_variable(ctx, property->value, NULL);
  }
}

static neo_js_variable_t neo_js_object_set_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field,
                                                 neo_js_variable_t value,
                                                 neo_js_variable_t receiver) {
  neo_js_object_property_t property =
      neo_js_object_get_own_property(ctx, self, field);
  neo_js_handle_t hobject = neo_js_variable_get_handle(self);
  neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
  if (!property) {
    property = neo_js_object_get_property(ctx, self, field);
    if (property && property->set) {
      neo_js_variable_t setter =
          neo_js_context_create_variable(ctx, property->set, NULL);
      return neo_js_context_call(ctx, setter, self, 1, &value);
    } else {
      return neo_js_context_def_field(ctx, self, field, value, true, true,
                                      true);
    }
  } else {
    if (property->value) {
      if (!property->writable) {
        const char *field_name = neo_js_context_to_error_name(ctx, field);
        size_t len = strlen(field_name) + 128;
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, len,
            "Cannot assign to read only property Symbol(%s) of object "
            "'#<Object>'",
            field_name);
      } else {
        if (property->value != hvalue) {
          neo_js_handle_remove_parent(property->value, hobject);
          neo_js_handle_add_parent(hvalue, hobject);
          neo_js_scope_t scope = neo_js_context_get_scope(ctx);
          neo_js_handle_t root = neo_js_scope_get_root_handle(scope);
          neo_js_handle_add_parent(property->value, root);
          property->value = hvalue;
        }
        return self;
      }
    } else if (property->set) {
      neo_js_variable_t setter =
          neo_js_context_create_variable(ctx, property->set, NULL);
      return neo_js_context_call(ctx, setter, receiver ? receiver : self, 1,
                                 &value);
    } else {
      const char *field_name = neo_js_context_to_error_name(ctx, field);
      size_t len = strlen(field_name) + 128;
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, len,
          "Cannot set property Symbol(%s) of #<Object> which has only "
          "a getter",
          field_name);
    }
  }
}

static neo_js_variable_t neo_js_object_del_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field) {
  if (neo_js_variable_get_type(field)->kind != NEO_JS_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_property_t property =
      neo_js_object_get_own_property(ctx, self, field);
  neo_js_object_t object =
      neo_js_value_to_object(neo_js_variable_get_value(self));
  const char *field_name = neo_js_context_to_error_name(ctx, field);
  size_t len = strlen(field_name) + 64;
  if (object->frozen || object->sealed) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len, "Cannot delete property %s of #<Object>",
        field_name);
  }
  if (property) {
    if (!property->configurable) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, len,
          "Cannot delete property '%s' of #<Object>", field_name);
    } else {
      neo_hash_map_delete(object->properties, neo_js_variable_get_handle(field),
                          ctx, ctx);
    }
  }
  return neo_js_context_create_boolean(ctx, true);
}

static bool neo_js_object_is_equal(neo_js_context_t ctx, neo_js_variable_t self,
                                   neo_js_variable_t another) {
  return neo_js_variable_get_value(self) == neo_js_variable_get_value(another);
}

static neo_js_variable_t neo_js_object_copy(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            neo_js_variable_t another) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_handle_t horigin = neo_js_variable_get_handle(self);
  neo_js_ref_t ref = neo_create_js_ref(allocator, horigin);
  neo_js_handle_t htarget = neo_js_variable_get_handle(another);
  neo_js_handle_add_parent(horigin, htarget);
  neo_js_handle_set_value(allocator, htarget, &ref->value);
  return another;
}

neo_js_type_t neo_get_js_object_type() {
  static struct _neo_js_type_t type = {
      NEO_JS_TYPE_OBJECT,      neo_js_object_typeof,
      neo_js_object_to_string, neo_js_object_to_boolean,
      neo_js_object_to_number, neo_js_object_to_primitive,
      neo_js_object_to_object, neo_js_object_get_field,
      neo_js_object_set_field, neo_js_object_del_field,
      neo_js_object_is_equal,  neo_js_object_copy,
  };
  return &type;
}

void neo_js_object_dispose(neo_allocator_t allocator, neo_js_object_t self) {
  neo_js_value_dispose(allocator, &self->value);
  neo_allocator_free(allocator, self->properties);
  neo_allocator_free(allocator, self->internal);
  neo_allocator_free(allocator, self->keys);
  neo_allocator_free(allocator, self->symbol_keys);
  neo_allocator_free(allocator, self->privates);
}

void neo_js_object_init(neo_allocator_t allocator, neo_js_object_t object) {
  neo_js_value_init(allocator, &object->value);
  object->value.type = neo_get_js_object_type();
  object->prototype = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.compare = (neo_compare_fn_t)neo_js_object_compare_key;
  initialize.hash = (neo_hash_fn_t)neo_js_object_key_hash;
  initialize.auto_free_value = true;
  object->properties = neo_create_hash_map(allocator, &initialize);
  initialize.auto_free_key = true;
  initialize.compare = (neo_compare_fn_t)strcmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  initialize.auto_free_value = false;
  object->internal = neo_create_hash_map(allocator, &initialize);
  object->frozen = false;
  object->extensible = true;
  object->sealed = false;
  object->constructor = NULL;
  object->keys = neo_create_list(allocator, NULL);
  object->symbol_keys = neo_create_list(allocator, NULL);
  object->privates = NULL;
}

int8_t neo_js_object_compare_key(neo_js_handle_t handle1,
                                 neo_js_handle_t handle2,
                                 neo_js_context_t ctx) {
  neo_js_variable_t val1 = neo_js_context_create_variable(ctx, handle1, NULL);
  neo_js_variable_t val2 = neo_js_context_create_variable(ctx, handle2, NULL);
  neo_js_variable_t result = neo_js_context_is_equal(ctx, val1, val2);
  neo_js_boolean_t boolean = neo_js_variable_to_boolean(result);
  return boolean->boolean ? 0 : 1;
}

uint32_t neo_js_object_key_hash(neo_js_handle_t handle, uint32_t max_bucket) {
  neo_js_value_t value = neo_js_handle_get_value(handle);
  if (value->type->kind == NEO_JS_TYPE_STRING) {
    return neo_hash_sdb_utf16(neo_js_value_to_string(value)->string,
                              max_bucket);
  } else {
    return (intptr_t)value % max_bucket;
  }
}

neo_js_object_t neo_create_js_object(neo_allocator_t allocator) {
  neo_js_object_t object = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_object_t), neo_js_object_dispose);
  neo_js_object_init(allocator, object);
  return object;
}

neo_js_object_t neo_js_value_to_object(neo_js_value_t value) {
  if (value->type->kind >= NEO_JS_TYPE_OBJECT) {
    return (neo_js_object_t)value;
  }
  return NULL;
}

neo_js_variable_t neo_js_object_set_prototype(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              neo_js_variable_t prototype) {
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "variable is not a object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(self);
  if (obj->sealed || obj->frozen || !obj->extensible) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "#<Object> is not extensible");
  }
  neo_js_handle_add_parent(obj->prototype, neo_js_scope_get_root_handle(
                                               neo_js_context_get_scope(ctx)));
  neo_js_handle_remove_parent(obj->prototype, neo_js_variable_get_handle(self));
  neo_js_handle_t hprototype = neo_js_variable_get_handle(prototype);
  obj->prototype = hprototype;
  neo_js_handle_add_parent(hprototype, neo_js_variable_get_handle(self));
  return neo_js_context_create_undefined(ctx);
}