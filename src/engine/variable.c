#include "engine/variable.h"
#include "core/allocator.h"
#include "core/list.h"
#include "engine/context.h"
#include "engine/object.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include <alloca.h>
#include <stdint.h>
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

neo_js_variable_t neo_js_variable_to_string(neo_js_variable_t self,
                                            neo_js_context_t ctx);

neo_js_variable_t neo_js_variable_to_number(neo_js_variable_t self,
                                            neo_js_context_t ctx);

neo_js_variable_t neo_js_variable_to_boolean(neo_js_variable_t self,
                                             neo_js_context_t ctx);

neo_js_variable_t neo_js_variable_to_object(neo_js_variable_t self,
                                            neo_js_context_t ctx);

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
    uint16_t *message =
        neo_js_context_format(ctx, "Cannot redefine property: %v", key);
    neo_js_variable_t msg = neo_js_context_create_string(ctx, message);
    neo_allocator_free(allocator, message);
    // TODO: msg -> error
    neo_js_variable_t error = msg;
    return neo_js_context_create_exception(ctx, error);
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_property_t prop =
      neo_hash_map_get(object->properties, key, ctx, ctx);
  if (!prop) {
    if (object->sealed || !object->extensible) {
      uint16_t *message = neo_js_context_format(
          ctx, "Cannot define property %v, object is not extensible", key);
      neo_js_variable_t msg = neo_js_context_create_string(ctx, message);
      neo_allocator_free(allocator, message);
      // TODO: msg -> error
      neo_js_variable_t error = msg;
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
      uint16_t *message =
          neo_js_context_format(ctx, "Cannot redefine property: %v", key);
      neo_js_variable_t msg = neo_js_context_create_string(ctx, message);
      neo_allocator_free(allocator, message);
      // TODO: msg -> error
      neo_js_variable_t error = msg;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_scope_t scope = neo_js_context_get_scope(ctx);
    if (prop->value) {
      neo_js_scope_set_variable(scope, prop->value, NULL);
      neo_js_variable_remove_parent(prop->value, self);
    }
    if (prop->get) {
      neo_js_scope_set_variable(scope, prop->get, NULL);
      neo_js_variable_remove_parent(prop->get, self);
    }
    if (prop->set) {
      neo_js_scope_set_variable(scope, prop->set, NULL);
      neo_js_variable_remove_parent(prop->set, self);
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
    uint16_t *message =
        neo_js_context_format(ctx, "Cannot redefine property: %v", key);
    neo_js_variable_t msg = neo_js_context_create_string(ctx, message);
    neo_allocator_free(allocator, message);
    // TODO: msg -> error
    neo_js_variable_t error = msg;
    return neo_js_context_create_exception(ctx, error);
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_property_t prop =
      neo_hash_map_get(object->properties, key, ctx, ctx);
  if (!prop) {
    if (object->sealed || !object->extensible) {
      uint16_t *message = neo_js_context_format(
          ctx, "Cannot define property %v, object is not extensible", key);
      neo_js_variable_t msg = neo_js_context_create_string(ctx, message);
      neo_allocator_free(allocator, message);
      // TODO: msg -> error
      neo_js_variable_t error = msg;
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
      uint16_t *message =
          neo_js_context_format(ctx, "Cannot redefine property: %v", key);
      neo_js_variable_t msg = neo_js_context_create_string(ctx, message);
      neo_allocator_free(allocator, message);
      // TODO: msg -> error
      neo_js_variable_t error = msg;
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_scope_t scope = neo_js_context_get_scope(ctx);
    if (prop->value) {
      neo_js_scope_set_variable(scope, prop->value, NULL);
      neo_js_variable_remove_parent(prop->value, self);
    }
    if (prop->get) {
      neo_js_scope_set_variable(scope, prop->get, NULL);
      neo_js_variable_remove_parent(prop->get, self);
    }
    if (prop->set) {
      neo_js_scope_set_variable(scope, prop->set, NULL);
      neo_js_variable_remove_parent(prop->set, self);
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
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_js_object_property_t prop = NULL;
  neo_js_object_t obj = object;
  while (obj) {
    prop = neo_hash_map_get(object->properties, key, ctx, ctx);
    if (prop) {
      break;
    }
    if (obj->prototype->value->type >= NEO_JS_TYPE_OBJECT) {
      obj = (neo_js_object_t)obj->prototype->value;
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

neo_js_variable_t neo_js_variable_set_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key,
                                            neo_js_variable_t value);