#include "engine/variable.h"
#include "core/allocator.h"
#include "core/bigint.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/bigint.h"
#include "engine/boolean.h"
#include "engine/cfunction.h"
#include "engine/context.h"
#include "engine/function.h"
#include "engine/number.h"
#include "engine/object.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/string.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void neo_js_variable_dispose(neo_allocator_t allocator,
                                    neo_js_variable_t variable) {
  if (variable->value && !--variable->value->ref) {
    neo_allocator_free(allocator, variable->value);
  }
  neo_allocator_free(allocator, variable->parents);
  neo_allocator_free(allocator, variable->children);
  neo_allocator_free(allocator, variable->weak_children);
  neo_allocator_free(allocator, variable->weak_parents);
}

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_value_t value) {
  neo_js_variable_t variable = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_variable_t), neo_js_variable_dispose);
  variable->allocator = allocator;
  variable->parents = neo_create_list(allocator, NULL);
  variable->children = neo_create_list(allocator, NULL);
  variable->weak_children = neo_create_list(allocator, NULL);
  variable->weak_parents = neo_create_list(allocator, NULL);
  variable->is_ = false;
  variable->is_using = false;
  variable->is_await_using = false;
  variable->ref = 0;
  variable->age = 0;
  variable->is_alive = true;
  variable->is_check = false;
  variable->is_disposed = false;
  variable->value = value;
  if (value) {
    value->ref++;
  }
  return variable;
}

void neo_js_variable_add_parent(neo_js_variable_t self,
                                neo_js_variable_t parent) {
  neo_list_push(self->parents, parent);
  neo_list_push(parent->children, self);
}

void neo_js_variable_remove_parent(neo_js_variable_t self,
                                   neo_js_variable_t parent) {
  neo_list_delete(self->parents, parent);
  neo_list_delete(parent->children, self);
}

void neo_js_variable_add_weak_parent(neo_js_variable_t self,
                                     neo_js_variable_t parent) {
  neo_list_push(self->weak_parents, parent);
  neo_list_push(parent->weak_children, self);
}

void neo_js_variable_remove_weak_parent(neo_js_variable_t self,
                                        neo_js_variable_t parent) {
  neo_list_delete(self->weak_parents, parent);
  neo_list_delete(parent->weak_children, self);
}

static void neo_js_variable_check(neo_allocator_t allocator,
                                  neo_js_variable_t self, uint32_t age) {
  if (self->age == age) {
    return;
  }
  self->age = age;
  if (self->ref) {
    self->is_alive = true;
    return;
  }
  self->is_check = true;
  self->is_alive = false;
  neo_list_node_t it = neo_list_get_first(self->parents);
  while (it != neo_list_get_tail(self->parents)) {
    neo_js_variable_t variable = neo_list_node_get(it);
    if (!variable->is_check) {
      neo_js_variable_check(allocator, variable, age);
      if (variable->is_alive) {
        self->is_alive = true;
        break;
      }
    }
    it = neo_list_node_next(it);
  }
  self->is_check = false;
}

void neo_js_variable_gc(neo_allocator_t allocator, neo_list_t gclist) {
  static uint32_t age = 0;
  ++age;
  neo_list_initialize_t initialize = {true};
  neo_list_t disposed = neo_create_list(allocator, &initialize);
  while (neo_list_get_size(gclist)) {
    neo_list_node_t it = neo_list_get_first(gclist);
    neo_js_variable_t variable = neo_list_node_get(it);
    neo_list_erase(gclist, it);
    neo_js_variable_check(allocator, variable, age);
    if (!variable->is_alive) {
      variable->is_disposed = true;
      neo_list_push(disposed, variable);
      neo_list_node_t it = neo_list_get_first(variable->children);
      while (it != neo_list_get_tail(variable->children)) {
        neo_js_variable_t child = neo_list_node_get(it);
        if (!child->is_disposed) {
          neo_list_push(gclist, child);
        }
        it = neo_list_node_next(it);
      }
    }
  }
  neo_allocator_free(allocator, disposed);
}

void neo_init_js_value(neo_js_value_t self, neo_allocator_t allocator,
                       neo_js_variable_type_t type) {
  self->type = type;
  self->ref = 0;
}

void neo_deinit_js_value(neo_js_value_t self, neo_allocator_t allocator) {}

neo_js_variable_t neo_js_variable_to_primitive(neo_js_variable_t self,
                                               neo_js_context_t ctx,
                                               const char *kind) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    return self;
  }
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  // TODO: [Symbol.toPrimitive]
  uint16_t *name = neo_string_to_string16(allocator, "valueOf");
  neo_js_variable_t key_name = neo_js_context_create_string(ctx, name);
  neo_allocator_free(allocator, name);
  neo_js_variable_t value_of = neo_js_variable_get_field(self, ctx, key_name);
  if (value_of->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value_of;
  }
  if (value_of->value->type == NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t result =
        neo_js_variable_call(value_of, ctx, self, 1, NULL);
    if (result->value->type == NEO_JS_TYPE_EXCEPTION) {
      return result;
    }
    if (result->value->type < NEO_JS_TYPE_OBJECT) {
      return result;
    }
  }

  name = neo_string_to_string16(allocator, "toString");
  key_name = neo_js_context_create_string(ctx, name);
  neo_allocator_free(allocator, name);
  neo_js_variable_t to_string = neo_js_variable_get_field(self, ctx, key_name);

  if (value_of->value->type == NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t result =
        neo_js_variable_call(to_string, ctx, self, 1, NULL);
    if (result->value->type == NEO_JS_TYPE_EXCEPTION) {
      return result;
    }
    if (result->value->type < NEO_JS_TYPE_OBJECT) {
      return result;
    }
  }
  uint16_t *msg = neo_string_to_string16(
      allocator, "Cannot convert object to primitive value");
  neo_js_variable_t message = neo_js_context_create_string(ctx, msg);
  neo_allocator_free(allocator, msg);
  // TODO: message -> error
  neo_js_variable_t error = message;
  return neo_js_context_create_exception(ctx, error);
}

neo_js_variable_t neo_js_variable_clone(neo_js_variable_t self,
                                        neo_js_context_t ctx) {
  switch (self->value->type) {
  case NEO_JS_TYPE_UNDEFINED:
    return neo_js_context_create_undefined(ctx);
  case NEO_JS_TYPE_NULL:
    return neo_js_context_create_null(ctx);
  case NEO_JS_TYPE_NUMBER:
    return neo_js_context_create_number(ctx,
                                        ((neo_js_number_t)self->value)->value);
  case NEO_JS_TYPE_BOOLEAN:
    return neo_js_context_create_boolean(
        ctx, ((neo_js_boolean_t)self->value)->value);
  case NEO_JS_TYPE_STRING:
    return neo_js_context_create_string(
        ctx, ((neo_js_string_t)(self->value))->value);
  default:
    return neo_js_context_create_variable(ctx, self->value);
  }
}

neo_js_variable_t neo_js_variable_to_string(neo_js_variable_t self,
                                            neo_js_context_t ctx) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  switch (self->value->type) {
  case NEO_JS_TYPE_UNDEFINED: {
    uint16_t *string = neo_string_to_string16(allocator, "undefined");
    neo_js_variable_t result = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    return result;
  }
  case NEO_JS_TYPE_NULL: {
    uint16_t *string = neo_string_to_string16(allocator, "null");
    neo_js_variable_t result = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    return result;
  }
  case NEO_JS_TYPE_NUMBER: {
    double value = ((neo_js_number_t)self->value)->value;
    char s[32];
    sprintf(s, "%g", value);
    uint16_t *string = neo_string_to_string16(allocator, s);
    neo_js_variable_t result = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    return result;
  }
  case NEO_JS_TYPE_BOOLEAN: {
    bool value = ((neo_js_boolean_t)self->value)->value;
    uint16_t *string =
        neo_string_to_string16(allocator, value ? "true" : "false");
    neo_js_variable_t result = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    return result;
  }
  case NEO_JS_TYPE_STRING:
    return neo_js_variable_clone(self, ctx);
  case NEO_JS_TYPE_BIGINT: {
    neo_bigint_t value = ((neo_js_bigint_t)self->value)->value;
    char *str = neo_bigint_to_string(value, 10);
    uint16_t *string = neo_string_to_string16(allocator, str);
    neo_allocator_free(allocator, str);
    neo_js_variable_t result = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    return result;
  }
  case NEO_JS_TYPE_SYMBOL: {
    uint16_t *string = neo_string_to_string16(
        allocator, "Cannot convert a Symbol value to a string");
    neo_js_variable_t message = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_ARRAY:
  case NEO_JS_TYPE_FUNCTION: {
    neo_js_variable_t primitive =
        neo_js_variable_to_primitive(self, ctx, "default");
    if (primitive->value->type == NEO_JS_TYPE_EXCEPTION) {
      return primitive;
    }
    return neo_js_variable_to_string(primitive, ctx);
  }
  default:
    return self;
  }
}

neo_js_variable_t neo_js_variable_to_number(neo_js_variable_t self,
                                            neo_js_context_t ctx) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  switch (self->value->type) {
  case NEO_JS_TYPE_UNDEFINED:
    return neo_js_context_create_number(ctx, NAN);
  case NEO_JS_TYPE_NULL:
    return neo_js_context_create_number(ctx, 0);
  case NEO_JS_TYPE_NUMBER:
    return neo_js_variable_clone(self, ctx);
  case NEO_JS_TYPE_BIGINT: {
    uint16_t *string = neo_string_to_string16(
        allocator,
        "Cannot mix BigInt and other types, use explicit conversions");
    neo_js_variable_t message = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  case NEO_JS_TYPE_BOOLEAN: {
    bool value = ((neo_js_bigint_t)(self->value))->value;
    return neo_js_context_create_boolean(ctx, value);
  }
  case NEO_JS_TYPE_STRING: {
    const uint16_t *string = ((neo_js_string_t)self->value)->value;
    while (IS_SPACE_SEPARATOR(*string) || *string == '\n' || *string == '\r') {
      string++;
    }
    if (!*string) {
      return neo_js_context_create_number(ctx, 0);
    }
    const char *inf = "Infinity";
    bool negative = false;
    const uint16_t *s = string;
    if (*s == '-') {
      negative = true;
      s++;
    } else if (*s == '+') {
      s++;
    }
    while (*inf) {
      if (*s != *inf) {
        break;
      }
      s++;
      inf++;
    }
    if (*inf == 0) {
      while (IS_SPACE_SEPARATOR(*s) || *s == '\n' || *s == '\r') {
        s++;
      }
      if (!*s) {
        if (negative) {
          return neo_js_context_create_number(ctx, -INFINITY);
        } else {
          return neo_js_context_create_number(ctx, INFINITY);
        }
      }
    }
    char *str = neo_string16_to_string(allocator, string);
    if (*str < '0' || *str > '9') {
      return neo_js_context_create_number(ctx, NAN);
    }
    if (*str == '.' && (str[1] < '0' || str[1] > '9')) {
      return neo_js_context_create_number(ctx, NAN);
    }
    double num = atof(str);
    neo_allocator_free(allocator, str);
    return neo_js_context_create_number(ctx, num);
  }
  case NEO_JS_TYPE_SYMBOL: {
    uint16_t *string = neo_string_to_string16(
        allocator, "Cannot convert a Symbol value to a number");
    neo_js_variable_t message = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_ARRAY:
  case NEO_JS_TYPE_FUNCTION: {
    neo_js_variable_t primitive =
        neo_js_variable_to_primitive(self, ctx, "number");
    if (primitive->value->type == NEO_JS_TYPE_EXCEPTION) {
      return primitive;
    }
    return neo_js_variable_to_number(primitive, ctx);
  } break;
  default:
    return self;
  }
}

neo_js_variable_t neo_js_variable_to_boolean(neo_js_variable_t self,
                                             neo_js_context_t ctx) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  switch (self->value->type) {
  case NEO_JS_TYPE_INTERRUPT:
  case NEO_JS_TYPE_EXCEPTION:
    return self;
  case NEO_JS_TYPE_UNDEFINED:
  case NEO_JS_TYPE_NULL:
    return neo_js_context_create_boolean(ctx, false);
  case NEO_JS_TYPE_NUMBER: {
    double val = ((neo_js_boolean_t)self->value)->value;
    return neo_js_context_create_boolean(ctx, val != 0 && val != -0);
  }
  case NEO_JS_TYPE_BIGINT: {
    neo_bigint_t val = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t zero = neo_number_to_bigint(allocator, 0);
    neo_js_variable_t res =
        neo_js_context_create_boolean(ctx, !neo_bigint_is_equal(val, zero));
    neo_allocator_free(allocator, zero);
    return res;
  }
  case NEO_JS_TYPE_BOOLEAN:
    return neo_js_variable_clone(self, ctx);
  case NEO_JS_TYPE_STRING: {
    const uint16_t *string = ((neo_js_string_t)self->value)->value;
    return neo_js_context_create_boolean(ctx, *string != 0);
  }
  case NEO_JS_TYPE_SYMBOL:
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_ARRAY:
  case NEO_JS_TYPE_FUNCTION:
    return neo_js_context_create_boolean(ctx, true);
  }
}

neo_js_variable_t neo_js_variable_to_object(neo_js_variable_t self,
                                            neo_js_context_t ctx) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    return neo_js_variable_clone(self, ctx);
  }
  // TODO: wrapper object
  return neo_js_context_create_object(ctx, NULL);
}

neo_js_variable_t
neo_js_variable_def_field(neo_js_variable_t self, neo_js_context_t ctx,
                          neo_js_variable_t key, neo_js_variable_t value,
                          bool configurable, bool enumable, bool writable) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  if (object->frozen) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot redefine property: %v", key);
    // TODO: msg -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
  } else {
    key = neo_js_variable_clone(key, ctx);
  }
  if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
    return key;
  }
  value = neo_js_variable_clone(value, ctx);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value;
  }
  neo_js_object_property_t prop =
      neo_hash_map_get(object->properties, key, ctx, ctx);
  if (!prop) {
    if (object->sealed || !object->extensible) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot define property %v, object is not extensible", key);
      // TODO: msg -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_object_property_t prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumable = enumable;
    prop->writable = writable;
    prop->value = value;
    neo_hash_map_set(object->properties, key, prop, ctx, ctx);
    neo_js_variable_add_parent(key, self);
  } else {
    if (!prop->configurable) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Cannot redefine property: %v", key);
      // TODO: msg -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_scope_t scope = neo_js_context_get_scope(ctx);
    if (prop->value) {
      neo_js_scope_set_variable(scope, prop->value, NULL);
      neo_js_variable_remove_parent(prop->value, self);
      prop->value = NULL;
    }
    if (prop->get) {
      neo_js_scope_set_variable(scope, prop->get, NULL);
      neo_js_variable_remove_parent(prop->get, self);
      prop->get = NULL;
    }
    if (prop->set) {
      neo_js_scope_set_variable(scope, prop->set, NULL);
      neo_js_variable_remove_parent(prop->set, self);
      prop->set = NULL;
    }
    prop->value = value;
    prop->configurable = configurable;
    prop->enumable = enumable;
    prop->writable = writable;
  }
  neo_js_variable_add_parent(value, self);
  return self;
}

neo_js_variable_t
neo_js_variable_def_accessor(neo_js_variable_t self, neo_js_context_t ctx,
                             neo_js_variable_t key, neo_js_variable_t get,
                             neo_js_variable_t set, bool configurable,
                             bool enumable) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  if (object->frozen) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot redefine property: %v", key);
    // TODO: msg -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
  } else {
    key = neo_js_variable_clone(key, ctx);
  }
  if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
    return key;
  }
  if (get) {
    get = neo_js_variable_clone(get, ctx);
    if (get->value->type == NEO_JS_TYPE_EXCEPTION) {
      return get;
    }
  }
  if (set) {
    set = neo_js_variable_clone(set, ctx);
    if (set->value->type == NEO_JS_TYPE_EXCEPTION) {
      return set;
    }
  }
  neo_js_object_property_t prop =
      neo_hash_map_get(object->properties, key, ctx, ctx);
  if (!prop) {
    if (object->sealed || !object->extensible) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot define property %v, object is not extensible", key);
      // TODO: msg -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_object_property_t prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumable = enumable;
    prop->get = get;
    prop->set = set;
    neo_hash_map_set(object->properties, key, prop, ctx, ctx);
    neo_js_variable_add_parent(key, self);
  } else {
    if (!prop->configurable) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Cannot redefine property: %v", key);
      // TODO: msg -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_scope_t scope = neo_js_context_get_scope(ctx);
    if (prop->value) {
      neo_js_scope_set_variable(scope, prop->value, NULL);
      neo_js_variable_remove_parent(prop->value, self);
      prop->value = NULL;
    }
    if (prop->get) {
      neo_js_scope_set_variable(scope, prop->get, NULL);
      neo_js_variable_remove_parent(prop->get, self);
      prop->get = NULL;
    }
    if (prop->set) {
      neo_js_scope_set_variable(scope, prop->set, NULL);
      neo_js_variable_remove_parent(prop->set, self);
      prop->set = NULL;
    }
    prop->get = get;
    prop->set = set;
    prop->configurable = configurable;
    prop->enumable = enumable;
  }
  neo_js_variable_add_parent(get, self);
  neo_js_variable_add_parent(set, self);
  return self;
}

neo_js_variable_t neo_js_variable_get_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_property_t prop = NULL;
  neo_js_object_t obj = (neo_js_object_t)self->value;
  while (obj) {
    prop = neo_hash_map_get(obj->properties, key, ctx, ctx);
    if (prop) {
      break;
    }
    self = obj->prototype;
    if (self->value->type >= NEO_JS_TYPE_OBJECT) {
      obj = (neo_js_object_t)self->value;
    } else {
      obj = NULL;
    }
  }
  if (prop) {
    if (prop->value) {
      return prop->value;
    } else if (prop->get) {
      return neo_js_variable_call(self, ctx, self, 0, NULL);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_variable_del_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_t obj = (neo_js_object_t)self->value;
  neo_hash_map_node_t it = neo_hash_map_find(obj->properties, key, ctx, ctx);
  if (it != neo_hash_map_get_tail(obj->properties)) {
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    neo_js_variable_t key = neo_hash_map_node_get_key(it);
    if (obj->sealed || obj->frozen || !prop->configurable) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot delete property '%v' of #<Object>", key);
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    if (prop->value) {
      neo_js_variable_remove_parent(prop->value, self);
      neo_js_context_recycle(ctx, prop->value);
    }
    if (prop->get) {
      neo_js_variable_remove_parent(prop->get, self);
      neo_js_context_recycle(ctx, prop->get);
    }
    if (prop->set) {
      neo_js_variable_remove_parent(prop->set, self);
      neo_js_context_recycle(ctx, prop->set);
    }
    neo_js_variable_remove_parent(key, self);
    neo_js_context_recycle(ctx, key);
    neo_hash_map_erase(obj->properties, it);
    return neo_js_context_create_boolean(ctx, true);
  }
  return neo_js_context_create_boolean(ctx, false);
}

neo_js_variable_t neo_js_variable_set_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key,
                                            neo_js_variable_t value) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
  } else {
    key = neo_js_variable_clone(key, ctx);
  }
  if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
    return key;
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_js_object_t receiver = object;
  neo_js_variable_t prototype = object->prototype;
  neo_js_object_property_t prop = NULL;
  while (object) {
    prop = neo_hash_map_get(object->properties, key, ctx, ctx);
    if (prop) {
      break;
    }
    prototype = object->prototype;
    if (prototype->value->type >= NEO_JS_TYPE_OBJECT) {
      object = (neo_js_object_t)prototype->value;
    } else {
      object = NULL;
    }
  }
  if (!prop) {
    return neo_js_variable_def_field(self, ctx, key, value, true, true, true);
  }
  if (!prop->configurable || !prop->writable || object->frozen ||
      receiver->frozen) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot assign to read only property '%v' of object '#<Object>'",
        key);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  value = neo_js_variable_clone(self, ctx);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value;
  }
  neo_js_scope_t scope = neo_js_context_get_scope(ctx);
  if (prop->value) {
    neo_js_scope_set_variable(scope, prop->value, NULL);
    neo_js_variable_remove_parent(prop->value, self);
    prop->value = NULL;
    prop->value = value;
    neo_js_variable_add_parent(value, self);
  } else if (prop->set && prop->set->value->type == NEO_JS_TYPE_FUNCTION) {
    return neo_js_variable_call(prop->set, ctx, self, 1, &value);
  }
  return value;
}

neo_js_variable_t neo_js_variable_call(neo_js_variable_t self,
                                       neo_js_context_t ctx,
                                       neo_js_variable_t func_self,
                                       uint32_t argc, neo_js_variable_t *argv) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a function", self);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_function_t func = (neo_js_function_t)self->value->type;
  neo_js_scope_t scope = neo_js_context_get_scope(ctx);
  neo_js_context_push_scope(ctx);
  neo_js_scope_t current_scope = neo_js_context_get_scope(ctx);
  for (uint32_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_scope_set_variable(current_scope, argv[idx], NULL);
  }
  neo_hash_map_t closure = func->closure;
  neo_hash_map_node_t it = neo_hash_map_get_first(closure);
  while (it != neo_hash_map_get_tail(closure)) {
    const uint16_t *name = neo_hash_map_node_get_key(it);
    neo_js_variable_t value = neo_hash_map_node_get_value(it);
    neo_js_scope_set_variable(current_scope, value, name);
    it = neo_hash_map_node_next(it);
  }
  func_self = neo_js_scope_set_variable(current_scope, func_self, NULL);
  neo_js_variable_t result = NULL;
  if (func->async) {
    // TODO: async function call
    neo_js_variable_t error = neo_js_context_format(ctx, "Not implement");
    result = neo_js_context_create_exception(ctx, error);
  }
  if (func->native) {
    neo_js_cfunction_t cfunc = (neo_js_cfunction_t)func;
    result = cfunc->callee(ctx, func_self, argc, argv);
  } else {
    // TODO: script function call
    neo_js_variable_t error = neo_js_context_format(ctx, "Not implement");
    result = neo_js_context_create_exception(ctx, error);
  }
  result = neo_js_scope_set_variable(scope, result, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}