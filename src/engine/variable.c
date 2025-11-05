#include "engine/variable.h"
#include "core/allocator.h"
#include "core/bigint.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "core/map.h"
#include "core/string.h"
#include "engine/bigint.h"
#include "engine/boolean.h"
#include "engine/cfunction.h"
#include "engine/context.h"
#include "engine/function.h"
#include "engine/handle.h"
#include "engine/number.h"
#include "engine/object.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/value.h"
#include "runtime/constant.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void neo_js_variable_dispose(neo_allocator_t allocator,
                                    neo_js_variable_t variable) {
  neo_deinit_js_handle(&variable->handle, allocator);
}

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_value_t value) {
  neo_js_variable_t variable = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_variable_t), neo_js_variable_dispose);
  neo_init_js_handle(&variable->handle, allocator);
  variable->is_using = false;
  variable->is_await_using = false;
  variable->is_const = false;
  variable->value = value;
  neo_js_handle_add_ref(&value->handle);
  return variable;
}

neo_js_variable_t neo_js_variable_to_string(neo_js_variable_t self,
                                            neo_js_context_t ctx) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  switch (self->value->type) {
  case NEO_JS_TYPE_UNDEFINED:
    return neo_js_context_create_cstring(ctx, "undefined");
  case NEO_JS_TYPE_NULL:
    return neo_js_context_create_cstring(ctx, "null");
  case NEO_JS_TYPE_NUMBER: {
    neo_js_number_t number = (neo_js_number_t)self->value;
    if (isnan(number->value)) {
      return neo_js_context_create_cstring(ctx, "NaN");
    }
    if (isinf(number->value)) {
      if (number->value < 0) {
        return neo_js_context_create_cstring(ctx, "-Infinity");
      } else {
        return neo_js_context_create_cstring(ctx, "Infinity");
      }
    }
    char s[16];
    sprintf(s, "%lg", number->value);
    return neo_js_context_create_cstring(ctx, s);
  }
  case NEO_JS_TYPE_BIGINT: {
    neo_js_bigint_t bigint = (neo_js_bigint_t)self->value;
    uint16_t *string = neo_bigint_to_string16(bigint->value, 10);
    return neo_js_context_create_string(ctx, string);
  }
  case NEO_JS_TYPE_BOOLEAN: {
    neo_js_boolean_t boolean = (neo_js_boolean_t)self->value;
    return neo_js_context_create_cstring(ctx,
                                         boolean->value ? "true" : "false");
  }
  case NEO_JS_TYPE_STRING:
    return self;
  case NEO_JS_TYPE_SYMBOL:
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_FUNCTION: {
    neo_js_variable_t primitive =
        neo_js_variable_to_primitive(self, ctx, "string");
    if (primitive->value->type == NEO_JS_TYPE_EXCEPTION) {
      return primitive;
    }
    return neo_js_variable_to_string(primitive, ctx);
  } break;
  default:
    return self;
  }
}

neo_js_variable_t neo_js_variable_to_primitive(neo_js_variable_t self,
                                               neo_js_context_t ctx,
                                               const char *hint) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    return self;
  }
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t key = constant->symbol_to_primitive;
  neo_js_variable_t to_primitive = neo_js_variable_get_field(self, ctx, key);
  if (to_primitive->value->type == NEO_JS_TYPE_EXCEPTION) {
    return to_primitive;
  }
  if (to_primitive->value->type == NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t vhint = neo_js_context_create_cstring(ctx, hint);
    neo_js_variable_t res =
        neo_js_variable_call(to_primitive, ctx, self, 1, &vhint);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    if (res->value->type >= NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot convert object to primitive value");
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
  }
  key = neo_js_context_create_cstring(ctx, "valueOf");
  neo_js_variable_t value_of = neo_js_variable_get_field(self, ctx, key);
  if (value_of->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value_of;
  }
  if (value_of->value->type == NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t res = neo_js_variable_call(value_of, ctx, self, 0, NULL);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    if (res->value->type < NEO_JS_TYPE_OBJECT) {
      return res;
    }
  }
  key = neo_js_context_create_cstring(ctx, "toString");
  neo_js_variable_t to_string = neo_js_variable_get_field(self, ctx, key);
  if (to_string->value->type == NEO_JS_TYPE_EXCEPTION) {
    return to_string;
  }
  if (to_string->value->type == NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t res = neo_js_variable_call(to_string, ctx, self, 0, NULL);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    if (res->value->type < NEO_JS_TYPE_OBJECT) {
      return res;
    }
  }
  neo_js_variable_t message =
      neo_js_context_format(ctx, "Cannot convert object to primitive value");
  // TODO: message -> error
  neo_js_variable_t error = message;
  return neo_js_context_create_exception(ctx, error);
}

neo_js_variable_t neo_js_variable_to_object(neo_js_variable_t self,
                                            neo_js_context_t ctx) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    return self;
  }
  // TODO: wrapper class constructor
  return self;
}

neo_js_variable_t
neo_js_variable_def_field(neo_js_variable_t self, neo_js_context_t ctx,
                          neo_js_variable_t key, neo_js_variable_t value,
                          bool configurable, bool enumable, bool writable) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "define field on non-object");
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t obj = (neo_js_object_t)self->value;
  if (obj->frozen) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot redefine property: %v", key);
    // TODO: msg -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_property_t prop =
      neo_hash_map_get(obj->properties, key->value, ctx, ctx);
  if (!prop) {
    if (obj->sealed || !obj->extensible) {
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
    prop->value = value->value;
    neo_hash_map_set(obj->properties, key->value, prop, ctx, ctx);
    neo_js_value_add_parent(key->value, self->value);
  } else {
    if (!prop->configurable) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Cannot redefine property: %v", key);
      // TODO: msg -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    if (prop->value) {
      neo_js_context_create_variable(ctx, prop->value);
      prop->value = NULL;
      prop->value = value->value;
    }
    if (prop->get) {
      neo_js_context_create_variable(ctx, prop->get);
      prop->get = NULL;
    }
    if (prop->set) {
      neo_js_context_create_variable(ctx, prop->get);
      prop->set = NULL;
    }
  }
  neo_js_value_add_parent(value->value, self->value);
  return self;
}

neo_js_variable_t
neo_js_variable_def_accessor(neo_js_variable_t self, neo_js_context_t ctx,
                             neo_js_variable_t key, neo_js_variable_t get,
                             neo_js_variable_t set, bool configurable,
                             bool enumable, bool writable) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "define field on non-object");
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t obj = (neo_js_object_t)self->value;
  if (obj->frozen) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot redefine property: %v", key);
    // TODO: msg -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_property_t prop =
      neo_hash_map_get(obj->properties, key->value, ctx, ctx);
  if (!prop) {
    if (obj->sealed || !obj->extensible) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot define property '%v', object is not extensible", key);
      // TODO: msg -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_object_property_t prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumable = enumable;
    prop->writable = writable;
    if (get) {
      prop->get = get->value;
    }
    if (set) {
      prop->set = set->value;
    }
    neo_hash_map_set(obj->properties, key->value, prop, ctx, ctx);
    neo_js_value_add_parent(key->value, self->value);
  } else {
    if (!prop->configurable) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Cannot redefine property: %v", key);
      // TODO: msg -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    if (prop->value) {
      neo_js_context_create_variable(ctx, prop->value);
      prop->value = NULL;
    }
    if (prop->get) {
      neo_js_context_create_variable(ctx, prop->get);
      if (get) {
        prop->get = get->value;
      }
    }
    if (prop->set) {
      neo_js_context_create_variable(ctx, prop->get);
      if (set) {
        prop->set = set->value;
      }
    }
  }
  if (get) {
    neo_js_value_add_parent(get->value, self->value);
  }
  if (set) {
    neo_js_value_add_parent(set->value, self->value);
  }
  return self;
}

neo_js_variable_t neo_js_variable_get_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_hash_map_node_t it =
      neo_hash_map_find(object->properties, key->value, ctx, ctx);
  while (it == neo_hash_map_get_tail(object->properties)) {
    if (object->prototype->type < NEO_JS_TYPE_OBJECT) {
      break;
    }
    object = (neo_js_object_t)object->prototype;
    it = neo_hash_map_find(object->properties, key->value, ctx, ctx);
  }
  if (it != neo_hash_map_get_tail(object->properties)) {
    neo_js_value_t key = neo_hash_map_node_get_key(it);
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    if (prop->value) {
      return neo_js_context_create_variable(ctx, prop->value);
    } else if (prop->get) {
      neo_js_variable_t get = neo_js_context_create_variable(ctx, prop->get);
      return neo_js_variable_call(get, ctx, self, 0, NULL);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_variable_set_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key,
                                            neo_js_variable_t value) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  if (object->frozen) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot assign to read only property '%v' of object '#<Object>'",
        key);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_property_t prop =
      neo_hash_map_get(object->properties, key, ctx, ctx);
  while (!prop) {
    if (object->prototype->type < NEO_JS_TYPE_OBJECT) {
      break;
    }
    object = (neo_js_object_t)object->prototype;
    prop = neo_hash_map_get(object->properties, key, ctx, ctx);
  }
  if (prop->value && neo_js_object_to_value(object) == self->value) {
    if (!prop->writable) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot assign to read only property '%v' of object '#<Object>'",
          key);
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_context_create_variable(ctx, prop->value);
    prop->value = value->value;
    neo_js_value_add_parent(value->value, self->value);
  } else if (prop && prop->set) {
    neo_js_variable_t set = neo_js_context_create_variable(ctx, prop->set);
    return neo_js_variable_call(set, ctx, self, 1, &value);
  } else if (prop && prop->get) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot set property '%v' of #<Object> which has only a getter",
        key);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  } else {
    return neo_js_variable_def_field(self, ctx, key, value, true, true, true);
  }
  return self;
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
  neo_js_object_t obj = (neo_js_object_t)self->value;
  neo_hash_map_node_t it =
      neo_hash_map_find(obj->properties, key->value, ctx, ctx);
  if (it != neo_hash_map_get_tail(obj->properties)) {
    neo_js_value_t key = neo_hash_map_node_get_key(it);
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    if (!prop->configurable || obj->frozen || obj->sealed) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot delete property '%v' of #<Object>", key);
      // TODO: msg -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_context_create_variable(ctx, key);
    neo_js_value_remove_parent(key, self->value);
    if (prop->value) {
      neo_js_context_create_variable(ctx, prop->value);
      neo_js_value_remove_parent(prop->value, self->value);
    }
    if (prop->get) {
      neo_js_context_create_variable(ctx, prop->get);
      neo_js_value_remove_parent(prop->get, self->value);
    }
    if (prop->get) {
      neo_js_context_create_variable(ctx, prop->get);
      neo_js_value_remove_parent(prop->get, self->value);
    }
    neo_hash_map_delete(obj->properties, key, ctx, ctx);
  }
  return self;
}

neo_js_variable_t neo_js_variable_call(neo_js_variable_t self,
                                       neo_js_context_t ctx,
                                       neo_js_variable_t bind, size_t argc,
                                       neo_js_variable_t *argv) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%s is not a function", self);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_function_t function = (neo_js_function_t)self->value;
  neo_js_scope_t origin_scope = neo_js_context_get_scope(ctx);
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_scope_t current_scope = neo_create_js_scope(allocator, root_scope);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_context_create_variable(ctx, argv[idx]->value);
  }
  bind = neo_js_context_create_variable(ctx, bind->value);
  neo_map_node_t it = neo_map_get_first(function->closure);
  while (it != neo_map_get_tail(function->closure)) {
    neo_js_variable_t variable = neo_map_node_get_value(it);
    const uint16_t *name = neo_map_node_get_key(it);
    neo_js_scope_set_variable(current_scope, variable, name);
    it = neo_map_node_next(it);
  }
  neo_js_variable_t result = NULL;
  if (function->async) {
    // TODO: async call
  } else {
    if (function->native) {
      neo_js_cfunction_t cfunction = (neo_js_cfunction_t)function;
      result = cfunction->callee(ctx, bind, argc, argv);
    } else {
      // TODO: script function
    }
  }
  neo_js_scope_set_variable(origin_scope, result, NULL);
  neo_js_context_set_scope(ctx, origin_scope);
  neo_allocator_free(allocator, current_scope);
  return result;
}

neo_js_variable_t neo_js_variable_get_internel(neo_js_variable_t self,
                                               neo_js_context_t ctx,
                                               const char *name) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a object", self);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_js_value_t value = neo_hash_map_get(object->internals, name, NULL, NULL);
  if (value) {
    return neo_js_context_create_variable(ctx, value);
  }
  return NULL;
}

neo_js_variable_t neo_js_variable_set_internal(neo_js_variable_t self,
                                               neo_js_context_t ctx,
                                               const char *name,
                                               neo_js_variable_t value) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a object", self);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_js_value_t current =
      neo_hash_map_get(object->internals, name, NULL, NULL);
  if (current) {
    neo_js_value_remove_parent(current, self->value);
    neo_js_context_create_variable(ctx, current);
  }
  neo_hash_map_set(object->internals, neo_create_string(allocator, name),
                   value->value, NULL, NULL);
  neo_js_value_add_parent(value->value, self->value);
  return self;
}

void neo_js_variable_set_opaque(neo_js_variable_t self, neo_js_context_t ctx,
                                const char *name, void *value) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_hash_map_set(self->value->opaque, neo_create_string(allocator, name),
                   value, NULL, NULL);
}

void *neo_js_variable_get_opaque(neo_js_variable_t self, neo_js_context_t ctx,
                                 const char *name) {
  return neo_hash_map_get(self->value->opaque, name, NULL, NULL);
}

neo_js_variable_t
neo_js_variable_set_prototype_of(neo_js_variable_t self, neo_js_context_t ctx,
                                 neo_js_variable_t prototype) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not object", self);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_js_value_remove_parent(object->prototype, self->value);
  neo_js_variable_t current =
      neo_js_context_create_variable(ctx, object->prototype);
  neo_js_value_add_parent(prototype->value, self->value);
  object->prototype = prototype->value;
  return current;
}
neo_js_variable_t neo_js_variable_get_prototype_of(neo_js_variable_t self,
                                                   neo_js_context_t ctx) {

  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not object", self);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  return neo_js_context_create_variable(ctx, object->prototype);
}

neo_js_variable_t neo_js_variable_extends(neo_js_variable_t self,
                                          neo_js_context_t ctx,
                                          neo_js_variable_t parent) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not function", self);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  if (parent->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not function", parent);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t parent_prototype =
      neo_js_variable_get_field(parent, ctx, constant->key_prototype);
  if (parent_prototype->value->type == NEO_JS_TYPE_EXCEPTION) {
    return parent_prototype;
  }
  neo_js_variable_t prototype =
      neo_js_variable_get_field(self, ctx, constant->key_prototype);
  if (prototype->value->type == NEO_JS_TYPE_EXCEPTION) {
    return prototype;
  }
  return neo_js_variable_set_prototype_of(prototype, ctx, parent_prototype);
}

neo_js_variable_t neo_js_variable_set_closure(neo_js_variable_t self,
                                              neo_js_context_t ctx,
                                              const uint16_t *name,
                                              neo_js_variable_t value) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not function", self);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_function_t function = (neo_js_function_t)self->value;
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_map_set(function->closure, neo_create_string16(allocator, name), value,
              ctx);
  neo_js_handle_add_parent(&value->handle, &self->handle);
  return self;
}

static void neo_js_variable_on_gc(neo_allocator_t allocator,
                                  neo_js_variable_t self, neo_list_t values) {
  if (neo_js_handle_dispose(&self->value->handle)) {
    neo_list_push(values, self->value);
  }
}

void neo_js_variable_gc(neo_allocator_t allocator, neo_list_t variables) {
  neo_list_initialize_t initalize = {true};
  neo_list_t destroyed = neo_create_list(allocator, &initalize);
  neo_list_t values = neo_create_list(allocator, NULL);
  neo_js_handle_gc(allocator, variables, destroyed,
                   (neo_js_handle_on_gc_fn_t)neo_js_variable_on_gc, values);
  neo_allocator_free(allocator, destroyed);
  neo_js_value_gc(allocator, values);
  neo_allocator_free(allocator, values);
}