#include "engine/variable.h"
#include "core/allocator.h"
#include "core/bigint.h"
#include "core/buffer.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "core/map.h"
#include "core/string.h"
#include "engine/bigint.h"
#include "engine/boolean.h"
#include "engine/callable.h"
#include "engine/cfunction.h"
#include "engine/context.h"
#include "engine/exception.h"
#include "engine/function.h"
#include "engine/handle.h"
#include "engine/interrupt.h"
#include "engine/number.h"
#include "engine/object.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/string.h"
#include "engine/value.h"
#include "runtime/array.h"
#include "runtime/constant.h"
#include "runtime/promise.h"
#include "runtime/vm.h"
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static void neo_js_variable_dispose(neo_allocator_t allocator,
                                    neo_js_variable_t variable) {
  neo_deinit_js_handle(&variable->handle, allocator);
}

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_value_t value) {
  neo_js_variable_t variable = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_variable_t), neo_js_variable_dispose);
  neo_init_js_handle(&variable->handle, allocator, NEO_JS_HANDLE_VARIABLE);
  variable->is_using = false;
  variable->is_await_using = false;
  variable->is_const = false;
  variable->value = value;
  neo_js_handle_add_parent(&value->handle, &variable->handle);
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
    if (number->value == (int64_t)(number->value)) {
      sprintf(s, "%" PRId64, (int64_t)number->value);
    } else {
      sprintf(s, "%lg", number->value);
    }
    return neo_js_context_create_cstring(ctx, s);
  }
  case NEO_JS_TYPE_BIGINT: {
    neo_js_bigint_t bigint = (neo_js_bigint_t)self->value;
    uint16_t *string = neo_bigint_to_string16(bigint->value, 10);
    neo_js_variable_t res = neo_js_context_create_string(ctx, string);
    neo_allocator_free(allocator, string);
    return res;
  }
  case NEO_JS_TYPE_BOOLEAN: {
    neo_js_boolean_t boolean = (neo_js_boolean_t)self->value;
    return neo_js_context_create_cstring(ctx,
                                         boolean->value ? "true" : "false");
  }
  case NEO_JS_TYPE_STRING:
    return self;
  case NEO_JS_TYPE_SYMBOL: {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot convert a Symbol value to a string");
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_CLASS:
  case NEO_JS_TYPE_ARRAY:
  case NEO_JS_TYPE_FUNCTION: {
    neo_js_variable_t primitive =
        neo_js_variable_to_primitive(self, ctx, "string");
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
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  switch (self->value->type) {
  case NEO_JS_TYPE_UNDEFINED:
    return constant->nan;
  case NEO_JS_TYPE_NULL:
    return neo_js_context_create_number(ctx, 0);
  case NEO_JS_TYPE_NUMBER:
    return self;
  case NEO_JS_TYPE_BIGINT: {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot convert a BigInt value to a number");
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  case NEO_JS_TYPE_BOOLEAN:
    return neo_js_context_create_number(ctx,
                                        ((neo_js_boolean_t)self->value)->value);
  case NEO_JS_TYPE_STRING: {
    const uint16_t *string = ((neo_js_string_t)self->value)->value;
    double value = neo_string16_to_number(string);
    return neo_js_context_create_number(ctx, value);
  }
  case NEO_JS_TYPE_SYMBOL: {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot convert a Symbol value to a number");
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_CLASS:
  case NEO_JS_TYPE_ARRAY:
  case NEO_JS_TYPE_FUNCTION: {
    neo_js_variable_t primitive =
        neo_js_variable_to_primitive(self, ctx, "number");
    if (primitive->value->type == NEO_JS_TYPE_EXCEPTION) {
      return primitive;
    }
    return neo_js_variable_to_number(primitive, ctx);
  }
  default:
    return self;
  }
}
neo_js_variable_t neo_js_variable_to_boolean(neo_js_variable_t self,
                                             neo_js_context_t ctx) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  switch (self->value->type) {
  case NEO_JS_TYPE_UNDEFINED:
  case NEO_JS_TYPE_NULL:
    return constant->boolean_false;
  case NEO_JS_TYPE_NUMBER: {
    double val = ((neo_js_number_t)self->value)->value;
    return (val == 0 || isnan(val)) ? constant->function_prototype
                                    : constant->boolean_true;
  }
  case NEO_JS_TYPE_BIGINT: {
    neo_bigint_t bigint = ((neo_js_bigint_t)self->value)->value;
    double val = neo_bigint_to_number(bigint);
    return val == 0 ? constant->boolean_false : constant->boolean_true;
  }
  case NEO_JS_TYPE_BOOLEAN:
    return self;
  case NEO_JS_TYPE_STRING: {
    const uint16_t *string = ((neo_js_string_t)self->value)->value;
    return *string ? constant->boolean_true : constant->boolean_false;
  }
  case NEO_JS_TYPE_SYMBOL:
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_CLASS:
  case NEO_JS_TYPE_ARRAY:
  case NEO_JS_TYPE_FUNCTION:
    return constant->boolean_true;
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
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
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
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t error = neo_js_variable_construct(
          constant->type_error_class, ctx, 1, &message);
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
  neo_js_variable_t error =
      neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
  return neo_js_context_create_exception(ctx, error);
}

neo_js_variable_t neo_js_variable_to_object(neo_js_variable_t self,
                                            neo_js_context_t ctx) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    return self;
  }
  // TODO: wrapper class constructor
  switch (self->value->type) {
  case NEO_JS_TYPE_NULL:
  case NEO_JS_TYPE_UNDEFINED: {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Cannot convert undefined or null to object");
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  case NEO_JS_TYPE_NUMBER:
    break;
  case NEO_JS_TYPE_BIGINT:
    break;
  case NEO_JS_TYPE_BOOLEAN:
    break;
  case NEO_JS_TYPE_STRING:
    break;
  case NEO_JS_TYPE_SYMBOL: {
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t object =
        neo_js_context_create_object(ctx, constant->symbol_prototype);
    neo_js_variable_set_internal(object, ctx, "PrimitiveValue", self);
    return object;
  }
  default:
    break;
  }
  return self;
}

neo_js_variable_t
neo_js_variable_def_field(neo_js_variable_t self, neo_js_context_t ctx,
                          neo_js_variable_t key, neo_js_variable_t value,
                          bool configurable, bool enumable, bool writable) {
  if (self->value->type == NEO_JS_TYPE_NULL) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot set properties of null (reading '%v')", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (self->value->type == NEO_JS_TYPE_UNDEFINED) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot set properties of undefined (reading '%v')", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "define field on non-object");
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t obj = (neo_js_object_t)self->value;
  if (obj->frozen) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot redefine property: %v", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_property_t prop = neo_hash_map_get(obj->properties, key->value);
  if (!prop) {
    if (obj->sealed || !obj->extensible) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot define property %v, object is not extensible", key);
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t error = neo_js_variable_construct(
          constant->type_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    if (self->value->type == NEO_JS_TYPE_ARRAY &&
        key->value->type == NEO_JS_TYPE_STRING) {
      neo_js_object_property_t length_prop = neo_hash_map_get(
          obj->properties, neo_js_context_create_cstring(ctx, "length")->value);
      neo_js_number_t current_length = (neo_js_number_t)length_prop->value;
      neo_js_variable_t index = neo_js_variable_to_number(key, ctx);
      double idxf = ((neo_js_number_t)index->value)->value;
      uint32_t idx = idxf;
      if (!isnan(idxf) && idx == idxf && idx >= current_length->value) {
        current_length->value = idx + 1;
      }
    }
    neo_js_object_property_t prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumable = enumable;
    prop->writable = writable;
    prop->value = value->value;
    neo_hash_map_set(obj->properties, key->value, prop);
    neo_js_value_add_parent(key->value, self->value);
    if (prop->enumable && key->value->type == NEO_JS_TYPE_STRING) {
      neo_list_push(obj->keys, key->value);
    }
  } else {
    if (!prop->configurable &&
        (prop->configurable != configurable || prop->enumable != enumable ||
         prop->writable != writable || !prop->value)) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Cannot redefine property: %v", key);
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t error = neo_js_variable_construct(
          constant->type_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    if (self->value->type == NEO_JS_TYPE_ARRAY &&
        key->value->type == NEO_JS_TYPE_STRING) {
      const uint16_t *string = ((neo_js_string_t)key->value)->value;
      if (neo_string16_mix_compare(string, "length") == 0) {
        neo_js_number_t current_length = ((neo_js_number_t)prop->value);
        value = neo_js_variable_to_number(value, ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          return value;
        }
        double lenf = ((neo_js_number_t)value->value)->value;
        uint32_t len = lenf;
        if (len != lenf) {
          neo_js_constant_t constant = neo_js_context_get_constant(ctx);
          neo_js_variable_t message =
              neo_js_context_format(ctx, "Invalid array length");
          neo_js_variable_t error = neo_js_variable_construct(
              constant->range_error_class, ctx, 1, &message);
          return neo_js_context_create_exception(ctx, error);
        }
        while (len < current_length->value) {
          neo_js_variable_t res = neo_js_variable_del_field(
              self, ctx,
              neo_js_context_create_number(ctx, current_length->value - 1));
          if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
            return res;
          }
          current_length->value--;
        }
        return self;
      } else {
        neo_js_object_property_t length_prop = neo_hash_map_get(
            obj->properties,
            neo_js_context_create_cstring(ctx, "length")->value);
        neo_js_number_t current_length = (neo_js_number_t)length_prop->value;
        neo_js_variable_t index = neo_js_variable_to_number(key, ctx);
        double idxf = ((neo_js_number_t)index->value)->value;
        uint32_t idx = idxf;
        if (!isnan(idxf) && idx == idxf && idx >= current_length->value) {
          current_length->value = idx + 1;
        }
      }
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
    prop->configurable = configurable;
    prop->enumable = enumable;
    prop->writable = writable;
  }
  neo_js_value_add_parent(value->value, self->value);
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
    neo_js_variable_t message =
        neo_js_context_format(ctx, "define field on non-object");
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t obj = (neo_js_object_t)self->value;
  if (obj->frozen) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot redefine property: %v", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_property_t prop = neo_hash_map_get(obj->properties, key->value);
  if (!prop) {
    if (obj->sealed || !obj->extensible) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot define property '%v', #<Object> is not extensible", key);
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t error = neo_js_variable_construct(
          constant->type_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    if (self->value->type == NEO_JS_TYPE_ARRAY &&
        key->value->type == NEO_JS_TYPE_STRING) {
      neo_js_object_property_t length_prop = neo_hash_map_get(
          obj->properties, neo_js_context_create_cstring(ctx, "length")->value);
      neo_js_number_t current_length = (neo_js_number_t)length_prop->value;
      neo_js_variable_t index = neo_js_variable_to_number(key, ctx);
      double idxf = ((neo_js_number_t)index->value)->value;
      uint32_t idx = idxf;
      if (!isnan(idxf) && idx == idxf && idx >= current_length->value) {
        current_length->value = idx + 1;
      }
    }
    neo_js_object_property_t prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumable = enumable;
    if (get) {
      prop->get = get->value;
    }
    if (set) {
      prop->set = set->value;
    }
    neo_hash_map_set(obj->properties, key->value, prop);
    neo_js_value_add_parent(key->value, self->value);
    if (prop->enumable && key->value->type == NEO_JS_TYPE_STRING) {
      neo_list_push(obj->keys, key->value);
    }
  } else {
    if (!prop->configurable) {
      if (prop->configurable != configurable || prop->enumable != enumable ||
          prop->value) {
        neo_js_variable_t message =
            neo_js_context_format(ctx, "Cannot redefine property: %v", key);
        neo_js_constant_t constant = neo_js_context_get_constant(ctx);
        neo_js_variable_t error = neo_js_variable_construct(
            constant->type_error_class, ctx, 1, &message);
        return neo_js_context_create_exception(ctx, error);
      }
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
    prop->configurable = configurable;
    prop->enumable = enumable;
  }
  if (get) {
    neo_js_value_add_parent(get->value, self->value);
  }
  if (set) {
    neo_js_value_add_parent(set->value, self->value);
  }
  return self;
}

neo_js_variable_t neo_js_variable_get_super_field(neo_js_variable_t self,
                                                  neo_js_context_t ctx,
                                                  neo_js_variable_t key) {
  neo_js_variable_t prototype = neo_js_variable_get_prototype_of(self, ctx);
  if (prototype->value->type == NEO_JS_TYPE_EXCEPTION) {
    return prototype;
  }
  prototype = neo_js_variable_get_prototype_of(self, ctx);
  if (prototype->value->type == NEO_JS_TYPE_EXCEPTION) {
    return prototype;
  }
  neo_js_object_property_t prop =
      neo_js_variable_get_property(prototype, ctx, key);
  if (prop) {
    if (prop->value) {
      return neo_js_context_create_variable(ctx, prop->value);
    }
    if (prop->get) {
      neo_js_variable_t get = neo_js_context_create_variable(ctx, prop->get);
      return neo_js_variable_call(get, ctx, self, 0, NULL);
    }
  }
  return neo_js_context_get_undefined(ctx);
}

neo_js_variable_t neo_js_variable_set_super_field(neo_js_variable_t self,
                                                  neo_js_context_t ctx,
                                                  neo_js_variable_t key,
                                                  neo_js_variable_t value) {
  neo_js_variable_t prototype = neo_js_variable_get_prototype_of(self, ctx);
  if (prototype->value->type == NEO_JS_TYPE_EXCEPTION) {
    return prototype;
  }
  prototype = neo_js_variable_get_prototype_of(self, ctx);
  if (prototype->value->type == NEO_JS_TYPE_EXCEPTION) {
    return prototype;
  }
  neo_js_object_property_t prop =
      neo_js_variable_get_property(prototype, ctx, key);
  if (!prop) {
    return neo_js_variable_set_field(self, ctx, key, value);
  }
  if (prop->set) {
    neo_js_variable_t set = neo_js_context_create_variable(ctx, prop->set);
    return neo_js_variable_call(set, ctx, self, 1, &value);
  }
  neo_js_variable_t message = neo_js_context_format(
      ctx, "Cannot set property %v of #<Object>, which has only a getter", key);
  neo_js_variable_t type_error =
      neo_js_context_get_constant(ctx)->type_error_class;
  neo_js_variable_t error =
      neo_js_variable_construct(type_error, ctx, 1, &message);
  return neo_js_context_create_exception(ctx, error);
}

neo_js_variable_t neo_js_variable_get_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key) {
  if (self->value->type == NEO_JS_TYPE_NULL) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot read properties of null (reading '%v')", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (self->value->type == NEO_JS_TYPE_UNDEFINED) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot read properties of undefined (reading '%v')", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
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
  neo_hash_map_node_t it = neo_hash_map_find(object->properties, key->value);
  while (!it) {
    if (object->prototype->type < NEO_JS_TYPE_OBJECT) {
      break;
    }
    object = (neo_js_object_t)object->prototype;
    it = neo_hash_map_find(object->properties, key->value);
  }
  if (it) {
    neo_js_value_t key = neo_hash_map_node_get_key(it);
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    if (prop->value) {
      return neo_js_context_create_variable(ctx, prop->value);
    } else if (prop->get) {
      neo_js_variable_t get = neo_js_context_create_variable(ctx, prop->get);
      return neo_js_variable_call(get, ctx, self, 0, NULL);
    }
  }
  return neo_js_context_get_undefined(ctx);
}

neo_js_object_property_t neo_js_variable_get_property(neo_js_variable_t self,
                                                      neo_js_context_t ctx,
                                                      neo_js_variable_t key) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    return NULL;
  }
  neo_js_object_property_t prop = NULL;
  neo_js_object_t object = (neo_js_object_t)self->value;
  object = (neo_js_object_t)self->value;
  prop = neo_hash_map_get(object->properties, key->value);
  while (!prop) {
    if (object->prototype->type < NEO_JS_TYPE_OBJECT) {
      break;
    }
    object = (neo_js_object_t)object->prototype;
    prop = neo_hash_map_get(object->properties, key->value);
  }
  return prop;
}
neo_js_object_property_t
neo_js_variable_get_own_property(neo_js_variable_t self, neo_js_context_t ctx,
                                 neo_js_variable_t key) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    return NULL;
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_js_object_property_t prop =
      neo_hash_map_get(object->properties, key->value);
  return prop;
}

neo_js_variable_t neo_js_variable_set_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key,
                                            neo_js_variable_t value) {
  if (self->value->type == NEO_JS_TYPE_NULL) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot set properties of null (reading '%v')", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (self->value->type == NEO_JS_TYPE_UNDEFINED) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot set properties of undefined (reading '%v')", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
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
  if (object->frozen) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Cannot assign to read only property '%v' of '#<Object>'", key);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_property_t prop =
      neo_hash_map_get(object->properties, key->value);
  while (!prop) {
    if (object->prototype->type < NEO_JS_TYPE_OBJECT) {
      break;
    }
    object = (neo_js_object_t)object->prototype;
    prop = neo_hash_map_get(object->properties, key->value);
  }
  if (prop && prop->value && neo_js_object_to_value(object) == self->value) {
    if (!prop->writable) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot assign to read only property '%v' of '#<Object>'", key);
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t error = neo_js_variable_construct(
          constant->type_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    if (self->value->type == NEO_JS_TYPE_ARRAY &&
        key->value->type == NEO_JS_TYPE_STRING) {
      const uint16_t *string = ((neo_js_string_t)key->value)->value;
      if (neo_string16_mix_compare(string, "length") == 0) {
        neo_js_number_t current_length = ((neo_js_number_t)prop->value);
        value = neo_js_variable_to_number(value, ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          return value;
        }
        double lenf = ((neo_js_number_t)value->value)->value;
        uint32_t len = lenf;
        if (len != lenf) {
          neo_js_constant_t constant = neo_js_context_get_constant(ctx);
          neo_js_variable_t message =
              neo_js_context_format(ctx, "Invalid array length");
          neo_js_variable_t error = neo_js_variable_construct(
              constant->range_error_class, ctx, 1, &message);
          return neo_js_context_create_exception(ctx, error);
        }
        while (len < current_length->value) {
          neo_js_variable_t res = neo_js_variable_del_field(
              self, ctx,
              neo_js_context_create_number(ctx, current_length->value - 1));
          if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
            return res;
          }
          current_length->value--;
        }
        return self;
      } else {
        neo_js_object_property_t length_prop = neo_hash_map_get(
            object->properties,
            neo_js_context_create_cstring(ctx, "length")->value);
        neo_js_number_t current_length = (neo_js_number_t)length_prop->value;
        neo_js_variable_t index = neo_js_variable_to_number(key, ctx);
        double idxf = ((neo_js_number_t)index->value)->value;
        uint32_t idx = idxf;
        if (!isnan(idxf) && idx == idxf && idx >= current_length->value) {
          current_length->value = idx + 1;
        }
      }
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
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
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
  if (key->value->type != NEO_JS_TYPE_SYMBOL) {
    key = neo_js_variable_to_string(key, ctx);
    if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
      return key;
    }
  }
  neo_js_object_t obj = (neo_js_object_t)self->value;
  neo_hash_map_node_t it = neo_hash_map_find(obj->properties, key->value);
  if (it) {
    neo_js_value_t key = neo_hash_map_node_get_key(it);
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    if (!prop->configurable || obj->frozen || obj->sealed) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Cannot delete property '%v' of #<Object>", key);
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t error = neo_js_variable_construct(
          constant->type_error_class, ctx, 1, &message);
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
    if (key->type == NEO_JS_TYPE_STRING && prop->enumable) {
      neo_list_delete(obj->keys, key);
    }
    neo_hash_map_delete(obj->properties, key);
  }
  return self;
}
static bool neo_js_variable_is_thenable(neo_js_variable_t self,
                                        neo_js_context_t ctx) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t then = neo_js_variable_get_field(
        self, ctx, neo_js_context_create_cstring(ctx, "then"));
    return then->value->type >= NEO_JS_TYPE_FUNCTION;
  }
  return false;
}
NEO_JS_CFUNCTION(neo_js_async_task);
NEO_JS_CFUNCTION(neo_js_async_onfulfilled) {
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_interrupt_t interr =
      (neo_js_interrupt_t)neo_js_context_load(ctx, "interrupt")->value;
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_scope_t scope = neo_js_context_set_scope(ctx, interr->scope);
  neo_js_variable_t interrupt = neo_js_context_create_interrupt(
      ctx, value, interr->address, interr->program, interr->vm,
      NEO_JS_INTERRUPT_AWAIT);
  interr->vm = NULL;
  neo_js_variable_t task =
      neo_js_context_create_cfunction(ctx, neo_js_async_task, NULL);
  neo_js_variable_set_closure(task, ctx, "promise", promise);
  neo_js_variable_set_closure(task, ctx, "interrupt", interrupt);
  neo_js_context_create_micro_task(ctx, task);
  neo_js_context_set_scope(ctx, scope);
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_async_onrejected) {
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_interrupt_t interr =
      (neo_js_interrupt_t)neo_js_context_load(ctx, "interrupt")->value;
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  value = neo_js_context_create_exception(ctx, value);
  neo_js_scope_t scope = neo_js_context_set_scope(ctx, interr->scope);
  neo_js_variable_t interrupt = neo_js_context_create_interrupt(
      ctx, value, interr->address, interr->program, interr->vm,
      NEO_JS_INTERRUPT_AWAIT);
  interr->vm = NULL;
  neo_js_variable_t task =
      neo_js_context_create_cfunction(ctx, neo_js_async_task, NULL);
  neo_js_variable_set_closure(task, ctx, "promise", promise);
  neo_js_variable_set_closure(task, ctx, "interrupt", interrupt);
  neo_js_context_create_micro_task(ctx, task);
  neo_js_context_set_scope(ctx, scope);
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_async_task) {
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_interrupt_t interrupt =
      (neo_js_interrupt_t)neo_js_context_load(ctx, "interrupt")->value;
  neo_js_scope_t scope = neo_js_context_set_scope(ctx, interrupt->scope);
  neo_js_variable_t value =
      neo_js_context_create_variable(ctx, interrupt->value);
  size_t address = interrupt->address;
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    interrupt->vm->result = value;
    address = neo_buffer_get_size(interrupt->program->codes);
  } else {
    neo_list_push(interrupt->vm->stack, value);
  }
  neo_js_variable_t next =
      neo_js_vm_run(interrupt->vm, ctx, interrupt->program, address);
  if (next->value->type == NEO_JS_TYPE_INTERRUPT) {
    interrupt->vm = NULL;
    interrupt = (neo_js_interrupt_t)next->value;
    neo_js_variable_t value =
        neo_js_context_create_variable(ctx, interrupt->value);
    neo_js_variable_t then = NULL;
    if (value->value->type >= NEO_JS_TYPE_OBJECT &&
        ((then = neo_js_variable_get_field(
              value, ctx, neo_js_context_create_cstring(ctx, "then")))
             ->value->type >= NEO_JS_TYPE_FUNCTION)) {
      neo_js_variable_t onfulfilled =
          neo_js_context_create_cfunction(ctx, neo_js_async_onfulfilled, NULL);
      neo_js_variable_set_closure(onfulfilled, ctx, "promise", promise);
      neo_js_variable_set_closure(onfulfilled, ctx, "interrupt", next);
      neo_js_variable_t onrejected =
          neo_js_context_create_cfunction(ctx, neo_js_async_onrejected, NULL);
      neo_js_variable_set_closure(onrejected, ctx, "promise", promise);
      neo_js_variable_set_closure(onrejected, ctx, "interrupt", next);
      neo_js_variable_t args[] = {onfulfilled, onrejected};
      neo_js_variable_call(then, ctx, value, 2, args);
    } else {
      neo_js_variable_t task =
          neo_js_context_create_cfunction(ctx, neo_js_async_task, NULL);
      neo_js_variable_set_closure(task, ctx, "promise", promise);
      neo_js_variable_set_closure(task, ctx, "interrupt", next);
      neo_js_context_create_micro_task(ctx, task);
    }
  } else {
    if (next->value->type == NEO_JS_TYPE_EXCEPTION) {
      neo_js_exception_t exception = (neo_js_exception_t)next->value;
      neo_js_variable_t error =
          neo_js_context_create_variable(ctx, exception->error);
      neo_js_promise_callback_reject(ctx, promise, 1, &error);
    } else {
      neo_js_promise_callback_resolve(ctx, promise, 1, &next);
    }
    while (neo_js_context_get_scope(ctx) !=
           neo_js_context_get_root_scope(ctx)) {
      neo_js_context_pop_scope(ctx);
    }
  }
  neo_js_context_set_scope(ctx, scope);
  return neo_js_context_get_undefined(ctx);
}

neo_js_variable_t neo_js_variable_call_function(neo_js_variable_t self,
                                                neo_js_context_t ctx,
                                                neo_js_variable_t bind,
                                                neo_js_variable_t arguments) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a function", self);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_callable_t callable = (neo_js_callable_t)self->value;
  neo_js_function_t function = (neo_js_function_t)callable;
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_scope_t scope = neo_create_js_scope(allocator, root_scope);
  neo_js_scope_set_variable(scope, arguments, "arguments");
  neo_js_scope_set_variable(scope, bind, NULL);
  neo_js_scope_t origin_scope = neo_js_context_set_scope(ctx, scope);
  neo_map_node_t it = neo_map_get_first(callable->closure);
  while (it != neo_map_get_tail(callable->closure)) {
    neo_js_variable_t variable = neo_map_node_get_value(it);
    const char *name = neo_map_node_get_key(it);
    neo_js_scope_set_variable(scope, variable, name);
    it = neo_map_node_next(it);
  }
  if (callable->is_generator && callable->is_async) {
    neo_js_variable_t prototype =
        neo_js_context_get_constant(ctx)->async_generator_prototype;
    neo_js_variable_t generator = neo_js_context_create_object(ctx, prototype);
    neo_js_vm_t vm = neo_create_js_vm(ctx, bind);
    neo_js_variable_t value = neo_js_context_create_interrupt(
        ctx, neo_js_context_get_undefined(ctx), function->address,
        function->program, vm, NEO_JS_INTERRUPT_INIT);
    neo_js_variable_set_internal(generator, ctx, "value", value);
    neo_js_scope_set_variable(origin_scope, generator, NULL);
    neo_js_context_set_scope(ctx, origin_scope);
    return generator;
  } else if (callable->is_generator) {
    neo_js_variable_t prototype =
        neo_js_context_get_constant(ctx)->generator_prototype;
    neo_js_variable_t generator = neo_js_context_create_object(ctx, prototype);
    neo_js_vm_t vm = neo_create_js_vm(ctx, bind);
    neo_js_variable_t value = neo_js_context_create_interrupt(
        ctx, neo_js_context_get_undefined(ctx), function->address,
        function->program, vm, NEO_JS_INTERRUPT_INIT);
    neo_js_variable_set_internal(generator, ctx, "value", value);
    neo_js_scope_set_variable(origin_scope, generator, NULL);
    neo_js_context_set_scope(ctx, origin_scope);
    return generator;
  } else if (callable->is_async) {
    neo_js_variable_t promise = neo_js_context_create_promise(ctx);
    neo_js_vm_t vm = neo_create_js_vm(ctx, bind);
    neo_js_variable_t interrupt = neo_js_context_create_interrupt(
        ctx, neo_js_context_get_undefined(ctx), function->address,
        function->program, vm, NEO_JS_INTERRUPT_INIT);
    neo_js_variable_t task =
        neo_js_context_create_cfunction(ctx, neo_js_async_task, NULL);
    neo_js_variable_set_closure(task, ctx, "promise", promise);
    neo_js_variable_set_closure(task, ctx, "interrupt", interrupt);
    neo_js_context_create_micro_task(ctx, task);
    neo_js_scope_set_variable(origin_scope, promise, NULL);
    neo_js_context_set_scope(ctx, origin_scope);
    return promise;
  } else {
    neo_js_vm_t vm = neo_create_js_vm(ctx, bind);
    neo_js_variable_t result =
        neo_js_vm_run(vm, ctx, function->program, function->address);
    neo_allocator_free(allocator, vm);
    neo_js_scope_set_variable(origin_scope, result, NULL);
    while (neo_js_context_get_scope(ctx) != root_scope) {
      neo_js_context_pop_scope(ctx);
    }
    neo_js_context_set_scope(ctx, origin_scope);
    return result;
  }
}

neo_js_variable_t neo_js_variable_call(neo_js_variable_t self,
                                       neo_js_context_t ctx,
                                       neo_js_variable_t bind, size_t argc,
                                       neo_js_variable_t *argv) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a function", self);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_callable_t callable = (neo_js_callable_t)self->value;
  bind = neo_js_context_create_variable(ctx, callable->bind ? callable->bind
                                                            : bind->value);
  if (!callable->is_native) {
    neo_js_variable_t arguments =
        neo_js_context_create_object(ctx, neo_js_context_get_null(ctx));
    for (size_t idx = 0; idx < argc; idx++) {
      neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t res =
          neo_js_variable_set_field(arguments, ctx, index, argv[idx]);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    }
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, "length");
    neo_js_variable_t length = neo_js_context_create_number(ctx, argc);
    neo_js_variable_set_field(arguments, ctx, key, length);
    neo_js_variable_t iterator =
        neo_js_context_create_cfunction(ctx, neo_js_array_values, "values");
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_set_field(arguments, ctx, constant->symbol_iterator,
                              iterator);
    return neo_js_variable_call_function(self, ctx, bind, arguments);
  }
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_scope_t current_scope = neo_create_js_scope(allocator, root_scope);
  neo_js_scope_t origin_scope = neo_js_context_set_scope(ctx, current_scope);
  neo_js_variable_t args[argc];
  for (size_t idx = 0; idx < argc; idx++) {
    args[idx] = neo_js_context_create_variable(ctx, argv[idx]->value);
  }
  neo_map_node_t it = neo_map_get_first(callable->closure);
  while (it != neo_map_get_tail(callable->closure)) {
    neo_js_variable_t variable = neo_map_node_get_value(it);
    const char *name = neo_map_node_get_key(it);
    neo_js_scope_set_variable(current_scope, variable, name);
    it = neo_map_node_next(it);
  }
  neo_js_cfunction_t cfunction = (neo_js_cfunction_t)callable;
  neo_js_context_type_t origin_ctx_type =
      neo_js_context_set_type(ctx, NEO_JS_CONTEXT_FUNCTION);
  neo_js_variable_t result = cfunction->callee(ctx, bind, argc, args);
  neo_js_context_set_type(ctx, origin_ctx_type);
  neo_js_scope_set_variable(origin_scope, result, NULL);
  while (neo_js_context_get_scope(ctx) != root_scope) {
    neo_js_context_pop_scope(ctx);
  }
  neo_js_context_set_scope(ctx, origin_scope);
  return result;
}

neo_js_variable_t neo_js_variable_construct(neo_js_variable_t self,
                                            neo_js_context_t ctx, size_t argc,
                                            neo_js_variable_t *argv) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION &&
      self->value->type != NEO_JS_TYPE_CLASS) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a constructor", self);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t prototype = neo_js_context_create_cstring(ctx, "prototype");
  prototype = neo_js_variable_get_field(self, ctx, prototype);
  if (prototype->value->type == NEO_JS_TYPE_EXCEPTION) {
    return prototype;
  }
  neo_js_variable_t object = neo_js_context_create_object(ctx, prototype);
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "constructor");
  neo_js_variable_def_field(object, ctx, key, self, true, false, true);
  neo_js_variable_t res = neo_js_variable_call(self, ctx, object, argc, argv);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    return res;
  }
  if (res->value->type >= NEO_JS_TYPE_OBJECT) {
    object = res;
  }
  neo_js_object_t obj = (neo_js_object_t)res->value;
  obj->clazz = self->value;
  neo_js_value_add_parent(obj->clazz, object->value);
  return object;
}

neo_js_variable_t neo_js_variable_get_internel(neo_js_variable_t self,
                                               neo_js_context_t ctx,
                                               const char *name) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a object", self);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_js_value_t value = neo_hash_map_get(object->internals, name);
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
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_object_t object = (neo_js_object_t)self->value;
  neo_js_value_t current = neo_hash_map_get(object->internals, name);
  if (current) {
    neo_js_value_remove_parent(current, self->value);
    neo_js_context_create_variable(ctx, current);
    neo_hash_map_delete(object->internals, name);
  }
  if (value) {
    neo_hash_map_set(object->internals, neo_create_string(allocator, name),
                     value->value);
    neo_js_value_add_parent(value->value, self->value);
  }
  return self;
}

void neo_js_variable_set_opaque(neo_js_variable_t self, neo_js_context_t ctx,
                                const char *name, void *value) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  if (value) {
    neo_hash_map_set(self->value->opaque, neo_create_string(allocator, name),
                     value);
  } else {
    neo_hash_map_delete(self->value->opaque, name);
  }
}

void *neo_js_variable_get_opaque(neo_js_variable_t self, neo_js_context_t ctx,
                                 const char *name) {
  return neo_hash_map_get(self->value->opaque, name);
}

neo_js_variable_t
neo_js_variable_set_prototype_of(neo_js_variable_t self, neo_js_context_t ctx,
                                 neo_js_variable_t prototype) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not object", self);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
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
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
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
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (parent->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not function", parent);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
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
  neo_js_variable_t error =
      neo_js_variable_set_prototype_of(prototype, ctx, parent_prototype);
  if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
    return error;
  }
  return neo_js_variable_set_prototype_of(self, ctx, parent);
}

neo_js_variable_t neo_js_variable_set_closure(neo_js_variable_t self,
                                              neo_js_context_t ctx,
                                              const char *name,
                                              neo_js_variable_t value) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not function", self);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_callable_t function = (neo_js_callable_t)self->value;
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_map_set(function->closure, neo_create_string(allocator, name), value);
  neo_js_handle_add_parent(&value->handle, &self->value->handle);
  return self;
}
neo_js_variable_t neo_js_variable_set_bind(neo_js_variable_t self,
                                           neo_js_context_t ctx,
                                           neo_js_variable_t bind) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not function", self);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_callable_t function = (neo_js_callable_t)self->value;
  function->bind = bind->value;
  neo_js_handle_add_parent(&function->bind->handle, &self->value->handle);
  return self;
}
neo_js_variable_t neo_js_variable_set_class(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t clazz) {
  if (self->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not function", self);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_callable_t function = (neo_js_callable_t)self->value;
  function->clazz = clazz->value;
  neo_js_handle_add_parent(&function->clazz->handle, &self->value->handle);
  return self;
}
static void neo_js_object_get_keys(neo_list_t keys, neo_hash_map_t cache,
                                   neo_js_object_t obj) {
  if (obj->prototype->type >= NEO_JS_TYPE_OBJECT) {
    neo_js_object_get_keys(keys, cache, (neo_js_object_t)obj->prototype);
  }
  for (neo_list_node_t it = neo_list_get_first(obj->keys);
       it != neo_list_get_tail(obj->keys); it = neo_list_node_next(it)) {
    neo_js_value_t val = neo_list_node_get(it);
    neo_hash_map_node_t node = neo_hash_map_find(cache, val);
    if (!node) {
      neo_list_push(keys, val);
      neo_hash_map_set(cache, val, NULL);
    } else {
      neo_js_value_t key = neo_hash_map_node_get_key(node);
      neo_list_delete(keys, key);
      neo_hash_map_delete(cache, key);
      neo_hash_map_set(cache, val, NULL);
    }
  }
}

static int neo_js_string_key_compare(neo_js_string_t key1,
                                     neo_js_string_t key2) {
  return neo_string16_compare(key1->value, key2->value);
}
static uint32_t neo_js_string_key_hash(neo_js_string_t key,
                                       uint32_t max_bucket) {
  return neo_hash_sdb_utf16(key->value, max_bucket);
}

static int neo_js_sort_key_compare(neo_js_string_t key1, neo_js_string_t key2) {
  double num1 = neo_string16_to_number(key1->value);
  double num2 = neo_string16_to_number(key2->value);
  if (num1 == (uint32_t)num1 && num2 == (uint32_t)num2) {
    return num1 - num2;
  }
  if (num1 == (uint32_t)num1) {
    return -1;
  }
  if (num2 == (uint32_t)num2) {
    return 1;
  }
  return 0;
}

neo_js_variable_t neo_js_variable_get_keys(neo_js_variable_t self,
                                           neo_js_context_t ctx) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_list_t keys = neo_create_list(allocator, NULL);
  neo_hash_map_initialize_t initialize = {0};
  initialize.compare = (neo_compare_fn_t)&neo_js_string_key_compare;
  initialize.hash = (neo_hash_fn_t)neo_js_string_key_hash;
  neo_hash_map_t cache = neo_create_hash_map(allocator, &initialize);
  neo_js_object_t obj = (neo_js_object_t)self->value;
  neo_js_object_get_keys(keys, cache, obj);
  if (neo_list_get_size(keys)) {
    neo_list_sort(neo_list_get_first(keys), neo_list_get_last(keys),
                  (neo_compare_fn_t)neo_js_sort_key_compare);
  }
  neo_js_variable_t res = neo_js_context_create_array(ctx);
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(keys);
       it != neo_list_get_tail(keys); it = neo_list_node_next(it)) {
    neo_js_value_t key = neo_list_node_get(it);
    neo_js_variable_t err = neo_js_variable_set_field(
        res, ctx, neo_js_context_create_number(ctx, idx),
        neo_js_context_create_variable(ctx, key));
    if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
      return err;
    }
    idx++;
  }
  neo_allocator_free(allocator, cache);
  neo_allocator_free(allocator, keys);
  return res;
}
neo_js_variable_t neo_js_variable_eq(neo_js_variable_t self,
                                     neo_js_context_t ctx,
                                     neo_js_variable_t another) {
  if (self->value->type != another->value->type) {
    if (self->value->type == NEO_JS_TYPE_UNDEFINED ||
        self->value->type == NEO_JS_TYPE_NULL) {
      if (another->value->type == NEO_JS_TYPE_UNDEFINED ||
          another->value->type == NEO_JS_TYPE_NULL) {
        return neo_js_context_get_true(ctx);
      } else {
        return neo_js_context_get_false(ctx);
      }
    }
    if (another->value->type >= NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t tmp = another;
      another = self;
      self = tmp;
    }
    if (self->value->type >= NEO_JS_TYPE_OBJECT &&
        another->value->type < NEO_JS_TYPE_OBJECT) {
      self = neo_js_variable_to_primitive(self, ctx, "default");
    }
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
    if (self->value->type >= NEO_JS_TYPE_OBJECT &&
        another->value->type >= NEO_JS_TYPE_OBJECT) {
      return self->value == another->value ? neo_js_context_get_true(ctx)
                                           : neo_js_context_get_false(ctx);
    }
    if (self->value->type != another->value->type) {
      if (self->value->type == NEO_JS_TYPE_SYMBOL ||
          another->value->type == NEO_JS_TYPE_SYMBOL) {
        return neo_js_context_get_false(ctx);
      }
      if (self->value->type == NEO_JS_TYPE_BOOLEAN) {
        another = neo_js_variable_to_boolean(another, ctx);
        return neo_js_variable_seq(self, ctx, another);
      }
      if (another->value->type == NEO_JS_TYPE_BOOLEAN) {
        self = neo_js_variable_to_boolean(self, ctx);
        return neo_js_variable_seq(self, ctx, another);
      }
      if (another->value->type == NEO_JS_TYPE_BIGINT) {
        neo_js_variable_t tmp = another;
        another = self;
        self = tmp;
      }
      if (self->value->type == NEO_JS_TYPE_BIGINT) {
        neo_bigint_t bigint = ((neo_js_bigint_t)self->value)->value;
        neo_allocator_t allocator =
            neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
        if (another->value->type == NEO_JS_TYPE_BOOLEAN) {
          bool val = ((neo_js_boolean_t)another->value)->value;
          neo_bigint_t value = neo_number_to_bigint(allocator, val);
          bool res = neo_bigint_is_equal(bigint, value);
          neo_allocator_free(allocator, value);
          return res ? neo_js_context_get_true(ctx)
                     : neo_js_context_get_false(ctx);
        } else if (another->value->type == NEO_JS_TYPE_NUMBER) {
          double val = ((neo_js_number_t)another->value)->value;
          neo_bigint_t value = neo_number_to_bigint(allocator, val);
          bool res = neo_bigint_is_equal(bigint, value);
          neo_allocator_free(allocator, value);
          return res ? neo_js_context_get_true(ctx)
                     : neo_js_context_get_false(ctx);
        } else {
          const uint16_t *val = ((neo_js_string_t)another->value)->value;
          neo_bigint_t value = neo_string16_to_bigint(allocator, val);
          bool res = neo_bigint_is_equal(bigint, value);
          neo_allocator_free(allocator, value);
          return res ? neo_js_context_get_true(ctx)
                     : neo_js_context_get_false(ctx);
        }
      }
      if (self->value->type != NEO_JS_TYPE_NUMBER) {
        self = neo_js_variable_to_number(self, ctx);
      }
      if (another->value->type != NEO_JS_TYPE_NUMBER) {
        another = neo_js_variable_to_number(self, ctx);
      }
    }
  }
  return neo_js_variable_seq(self, ctx, another);
}
neo_js_variable_t neo_js_variable_seq(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  switch (self->value->type) {
  case NEO_JS_TYPE_UNDEFINED:
    return neo_js_context_get_true(ctx);
  case NEO_JS_TYPE_NULL:
    return neo_js_context_get_true(ctx);
  case NEO_JS_TYPE_NUMBER: {
    double left = ((neo_js_number_t)self->value)->value;
    double right = ((neo_js_number_t)another->value)->value;
    return left == right ? neo_js_context_get_true(ctx)
                         : neo_js_context_get_false(ctx);
  }
  case NEO_JS_TYPE_BIGINT: {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    return neo_bigint_is_equal(left, right) ? neo_js_context_get_true(ctx)
                                            : neo_js_context_get_false(ctx);
  }
  case NEO_JS_TYPE_BOOLEAN: {
    bool left = ((neo_js_boolean_t)self->value)->value;
    bool right = ((neo_js_boolean_t)another->value)->value;
    return left == right ? neo_js_context_get_true(ctx)
                         : neo_js_context_get_false(ctx);
  }
  case NEO_JS_TYPE_STRING: {
    const uint16_t *left = ((neo_js_string_t)self->value)->value;
    const uint16_t *right = ((neo_js_string_t)another->value)->value;
    return neo_string16_compare(left, right) == 0
               ? neo_js_context_get_true(ctx)
               : neo_js_context_get_false(ctx);
  }
  case NEO_JS_TYPE_SYMBOL:
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_CLASS:
  case NEO_JS_TYPE_ARRAY:
  case NEO_JS_TYPE_FUNCTION:
    return self->value == another->value ? neo_js_context_get_true(ctx)
                                         : neo_js_context_get_false(ctx);
  default:
    return self;
  }
}

neo_js_variable_t neo_js_variable_gt(neo_js_variable_t self,
                                     neo_js_context_t ctx,
                                     neo_js_variable_t another) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (another->value->type >= NEO_JS_TYPE_OBJECT) {
    another = neo_js_variable_to_primitive(another, ctx, "default");
  }
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  if (another->value->type == NEO_JS_TYPE_BIGINT ||
      self->value->type == NEO_JS_TYPE_BIGINT) {
    if (another->value->type != NEO_JS_TYPE_BIGINT ||
        self->value->type != NEO_JS_TYPE_BIGINT) {
      neo_js_variable_t message = neo_js_context_create_cstring(
          ctx, "Cannot mix BigInt and other types, use explicit conversions");
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    return neo_bigint_is_greater(left, right) ? neo_js_context_get_true(ctx)
                                              : neo_js_context_get_false(ctx);
  }
  self = neo_js_variable_to_number(self, ctx);
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  another = neo_js_variable_to_number(self, ctx);
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)self->value)->value;
  return left > right ? neo_js_context_get_true(ctx)
                      : neo_js_context_get_false(ctx);
}
neo_js_variable_t neo_js_variable_lt(neo_js_variable_t self,
                                     neo_js_context_t ctx,
                                     neo_js_variable_t another) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (another->value->type >= NEO_JS_TYPE_OBJECT) {
    another = neo_js_variable_to_primitive(another, ctx, "default");
  }
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  if (another->value->type == NEO_JS_TYPE_BIGINT ||
      self->value->type == NEO_JS_TYPE_BIGINT) {
    if (another->value->type != NEO_JS_TYPE_BIGINT ||
        self->value->type != NEO_JS_TYPE_BIGINT) {
      neo_js_variable_t message = neo_js_context_create_cstring(
          ctx, "Cannot mix BigInt and other types, use explicit conversions");
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    return neo_bigint_is_less(left, right) ? neo_js_context_get_true(ctx)
                                           : neo_js_context_get_false(ctx);
  }
  self = neo_js_variable_to_number(self, ctx);
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  another = neo_js_variable_to_number(self, ctx);
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)self->value)->value;
  return left < right ? neo_js_context_get_true(ctx)
                      : neo_js_context_get_false(ctx);
}
neo_js_variable_t neo_js_variable_ge(neo_js_variable_t self,
                                     neo_js_context_t ctx,
                                     neo_js_variable_t another) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (another->value->type >= NEO_JS_TYPE_OBJECT) {
    another = neo_js_variable_to_primitive(another, ctx, "default");
  }
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  if (another->value->type == NEO_JS_TYPE_BIGINT ||
      self->value->type == NEO_JS_TYPE_BIGINT) {
    if (another->value->type != NEO_JS_TYPE_BIGINT ||
        self->value->type != NEO_JS_TYPE_BIGINT) {
      neo_js_variable_t message = neo_js_context_create_cstring(
          ctx, "Cannot mix BigInt and other types, use explicit conversions");
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    return neo_bigint_is_greater_or_equal(left, right)
               ? neo_js_context_get_true(ctx)
               : neo_js_context_get_false(ctx);
  }
  self = neo_js_variable_to_number(self, ctx);
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  another = neo_js_variable_to_number(self, ctx);
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)self->value)->value;
  return left >= right ? neo_js_context_get_true(ctx)
                       : neo_js_context_get_false(ctx);
}
neo_js_variable_t neo_js_variable_le(neo_js_variable_t self,
                                     neo_js_context_t ctx,
                                     neo_js_variable_t another) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (another->value->type >= NEO_JS_TYPE_OBJECT) {
    another = neo_js_variable_to_primitive(another, ctx, "default");
  }
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  if (another->value->type == NEO_JS_TYPE_BIGINT ||
      self->value->type == NEO_JS_TYPE_BIGINT) {
    if (another->value->type != NEO_JS_TYPE_BIGINT ||
        self->value->type != NEO_JS_TYPE_BIGINT) {
      neo_js_variable_t message = neo_js_context_create_cstring(
          ctx, "Cannot mix BigInt and other types, use explicit conversions");
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    return neo_bigint_is_less_or_equal(left, right)
               ? neo_js_context_get_true(ctx)
               : neo_js_context_get_false(ctx);
  }
  self = neo_js_variable_to_number(self, ctx);
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  another = neo_js_variable_to_number(self, ctx);
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)self->value)->value;
  return left <= right ? neo_js_context_get_true(ctx)
                       : neo_js_context_get_false(ctx);
}

neo_js_variable_t neo_js_variable_typeof(neo_js_variable_t self,
                                         neo_js_context_t ctx) {
  switch (self->value->type) {
  case NEO_JS_TYPE_UNDEFINED:
    return neo_js_context_create_cstring(ctx, "undefined");
  case NEO_JS_TYPE_NULL:
    return neo_js_context_create_cstring(ctx, "object");
  case NEO_JS_TYPE_NUMBER:
    return neo_js_context_create_cstring(ctx, "number");
  case NEO_JS_TYPE_BIGINT:
    return neo_js_context_create_cstring(ctx, "bigint");
  case NEO_JS_TYPE_BOOLEAN:
    return neo_js_context_create_cstring(ctx, "boolean");
  case NEO_JS_TYPE_STRING:
    return neo_js_context_create_cstring(ctx, "string");
  case NEO_JS_TYPE_SYMBOL:
    return neo_js_context_create_cstring(ctx, "symbol");
  case NEO_JS_TYPE_OBJECT:
  case NEO_JS_TYPE_ARRAY:
    return neo_js_context_create_cstring(ctx, "object");
  case NEO_JS_TYPE_CLASS:
  case NEO_JS_TYPE_FUNCTION:
    return neo_js_context_create_cstring(ctx, "function");
  default:
    return self;
  }
}
neo_js_variable_t neo_js_variable_inc(neo_js_variable_t self,
                                      neo_js_context_t ctx) {
  neo_js_variable_t val = self;
  if (val->value->type >= NEO_JS_TYPE_OBJECT) {
    val = neo_js_variable_to_primitive(val, ctx, "default");
  }
  if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
    return val;
  }
  val = neo_js_variable_to_number(val, ctx);
  if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
    return val;
  }
  double value = ((neo_js_number_t)val->value)->value;
  value += 1;
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_number_t num = neo_create_js_number(allocator, value);
  neo_js_context_create_variable(ctx, self->value);
  neo_js_handle_remove_parent(&self->value->handle, &self->handle);
  self->value = &num->super;
  neo_js_handle_add_parent(&self->value->handle, &self->handle);
  return self;
}
neo_js_variable_t neo_js_variable_dec(neo_js_variable_t self,
                                      neo_js_context_t ctx) {
  neo_js_variable_t val = self;
  if (val->value->type >= NEO_JS_TYPE_OBJECT) {
    val = neo_js_variable_to_primitive(val, ctx, "default");
  }
  if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
    return val;
  }
  val = neo_js_variable_to_number(val, ctx);
  if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
    return val;
  }
  double value = ((neo_js_number_t)val->value)->value;
  value -= 1;
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_number_t num = neo_create_js_number(allocator, value);
  neo_js_context_create_variable(ctx, self->value);
  neo_js_handle_remove_parent(&self->value->handle, &self->handle);
  self->value = &num->super;
  neo_js_handle_add_parent(&self->value->handle, &self->handle);
  return self;
}

neo_js_variable_t neo_js_variable_add(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (another->value->type >= NEO_JS_TYPE_OBJECT) {
    another = neo_js_variable_to_primitive(another, ctx, "default");
  }
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  if (self->value->type != another->value->type) {
    if (self->value->type == NEO_JS_TYPE_STRING) {
      another = neo_js_variable_to_string(another, ctx);
      if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
        return another;
      }
    } else if (another->value->type == NEO_JS_TYPE_STRING) {
      self = neo_js_variable_to_string(self, ctx);
      if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
        return self;
      }
    } else if (self->value->type == NEO_JS_TYPE_BIGINT ||
               another->value->type == NEO_JS_TYPE_BIGINT) {
      neo_js_variable_t message = neo_js_context_create_cstring(
          ctx, "Cannot mix BigInt and other types, use explicit conversions");
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_add(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  if (self->value->type == NEO_JS_TYPE_STRING) {
    const uint16_t *left = ((neo_js_string_t)self->value)->value;
    const uint16_t *right = ((neo_js_string_t)another->value)->value;
    size_t leftlen = neo_string16_length(left);
    size_t rightlen = neo_string16_length(right);
    uint16_t res[leftlen + rightlen + 1];
    memcpy(res, left, leftlen * sizeof(uint16_t));
    memcpy(res + leftlen, right, sizeof(uint16_t) * rightlen);
    res[leftlen + rightlen] = 0;
    return neo_js_context_create_string(ctx, res);
  }
  if (self->value->type != NEO_JS_TYPE_NUMBER) {
    self = neo_js_variable_to_number(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (another->value->type != NEO_JS_TYPE_NUMBER) {
    another = neo_js_variable_to_number(another, ctx);
  }
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)another->value)->value;
  double res = left + right;
  return neo_js_context_create_number(ctx, res);
}

static neo_js_variable_t
neo_js_variable_resolve_binary(neo_js_variable_t self, neo_js_context_t ctx,
                               neo_js_variable_t another) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (another->value->type >= NEO_JS_TYPE_OBJECT) {
    another = neo_js_variable_to_primitive(another, ctx, "default");
  }
  if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
    return another;
  }
  if ((self->value->type == NEO_JS_TYPE_BIGINT ||
       another->value->type == NEO_JS_TYPE_BIGINT) &&
      self->value->type != another->value->type) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Cannot mix BigInt and other types, use explicit conversions");
    neo_js_variable_t type_error =
        neo_js_context_get_constant(ctx)->type_error_class;
    neo_js_variable_t error =
        neo_js_variable_construct(type_error, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (self->value->type != NEO_JS_TYPE_BIGINT) {
    if (self->value->type != NEO_JS_TYPE_NUMBER) {
      self = neo_js_variable_to_number(self, ctx);
    }
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
    if (another->value->type != NEO_JS_TYPE_NUMBER) {
      another = neo_js_variable_to_number(another, ctx);
    }
    if (another->value->type == NEO_JS_TYPE_EXCEPTION) {
      return another;
    }
  }
  return self;
}
neo_js_variable_t neo_js_variable_sub(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_sub(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)another->value)->value;
  double res = left - right;
  return neo_js_context_create_number(ctx, res);
}
neo_js_variable_t neo_js_variable_mul(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_mul(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)another->value)->value;
  double res = left * right;
  return neo_js_context_create_number(ctx, res);
}
neo_js_variable_t neo_js_variable_div(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_div(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)another->value)->value;
  double res = left / right;
  return neo_js_context_create_number(ctx, res);
}
neo_js_variable_t neo_js_variable_mod(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_mod(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)another->value)->value;
  double res = 0;
  if (left == (int64_t)left && right == (int64_t)right) {
    res = (int64_t)left % (int64_t)right;
  } else {
    int64_t count = (int64_t)(left / right);
    res = left - right * count;
  }
  return neo_js_context_create_number(ctx, res);
}
neo_js_variable_t neo_js_variable_pow(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_pow(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  double left = ((neo_js_number_t)self->value)->value;
  double right = ((neo_js_number_t)another->value)->value;
  double res = pow(left, right);
  return neo_js_context_create_number(ctx, res);
}

neo_js_variable_t neo_js_variable_not(neo_js_variable_t self,
                                      neo_js_context_t ctx) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t right = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t res = neo_bigint_not(right);
    return neo_js_context_create_bigint(ctx, res);
  }
  if (self->value->type != NEO_JS_TYPE_NUMBER) {
    self = neo_js_variable_to_number(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  int32_t val = ((neo_js_number_t)self->value)->value;
  val = ~val;
  return neo_js_context_create_number(ctx, val);
}

neo_js_variable_t neo_js_variable_and(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_and(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  int32_t left = ((neo_js_number_t)self->value)->value;
  int32_t right = ((neo_js_number_t)another->value)->value;
  return neo_js_context_create_number(ctx, left & right);
}

neo_js_variable_t neo_js_variable_or(neo_js_variable_t self,
                                     neo_js_context_t ctx,
                                     neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_or(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  int32_t left = ((neo_js_number_t)self->value)->value;
  int32_t right = ((neo_js_number_t)another->value)->value;
  return neo_js_context_create_number(ctx, left | right);
}

neo_js_variable_t neo_js_variable_xor(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_xor(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  int32_t left = ((neo_js_number_t)self->value)->value;
  int32_t right = ((neo_js_number_t)another->value)->value;
  return neo_js_context_create_number(ctx, left ^ right);
}

neo_js_variable_t neo_js_variable_shl(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_shl(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  int32_t left = ((neo_js_number_t)self->value)->value;
  int32_t right = ((neo_js_number_t)another->value)->value;
  return neo_js_context_create_number(ctx, left << right);
}

neo_js_variable_t neo_js_variable_shr(neo_js_variable_t self,
                                      neo_js_context_t ctx,
                                      neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t left = ((neo_js_bigint_t)self->value)->value;
    neo_bigint_t right = ((neo_js_bigint_t)another->value)->value;
    neo_bigint_t res = neo_bigint_shr(left, right);
    return neo_js_context_create_bigint(ctx, res);
  }
  int32_t left = ((neo_js_number_t)self->value)->value;
  int32_t right = ((neo_js_number_t)another->value)->value;
  return neo_js_context_create_number(ctx, left >> right);
}

neo_js_variable_t neo_js_variable_ushr(neo_js_variable_t self,
                                       neo_js_context_t ctx,
                                       neo_js_variable_t another) {
  neo_js_variable_t err = neo_js_variable_resolve_binary(self, ctx, another);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "BigInts have no unsigned right shift, use >> instead");
    neo_js_variable_t type_error =
        neo_js_context_get_constant(ctx)->type_error_class;
    neo_js_variable_t error =
        neo_js_variable_construct(type_error, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  int32_t left = ((neo_js_number_t)self->value)->value;
  int32_t right = ((neo_js_number_t)another->value)->value;
  return neo_js_context_create_number(
      ctx, (int32_t)(((int64_t)(*(uint32_t *)&left)) >> right));
}

neo_js_variable_t neo_js_variable_plus(neo_js_variable_t self,
                                       neo_js_context_t ctx) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (self->value->type != NEO_JS_TYPE_NUMBER) {
    self = neo_js_variable_to_number(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  double left = ((neo_js_number_t)self->value)->value;
  return neo_js_context_create_number(ctx, left);
}
neo_js_variable_t neo_js_variable_neg(neo_js_variable_t self,
                                      neo_js_context_t ctx) {
  if (self->value->type >= NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_primitive(self, ctx, "default");
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  if (self->value->type == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t val = ((neo_js_bigint_t)self->value)->value;
    val = neo_bigint_neg(val);
    return neo_js_context_create_bigint(ctx, val);
  }
  if (self->value->type != NEO_JS_TYPE_NUMBER) {
    self = neo_js_variable_to_number(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  double left = ((neo_js_number_t)self->value)->value;
  return neo_js_context_create_number(ctx, -left);
}
neo_js_variable_t neo_js_variable_instance_of(neo_js_variable_t self,
                                              neo_js_context_t ctx,
                                              neo_js_variable_t another) {
  if (another->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Right-hand side of 'instanceof' is not an object");
    neo_js_variable_t type_error =
        neo_js_context_get_constant(ctx)->type_error_class;
    neo_js_variable_t error =
        neo_js_variable_construct(type_error, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (another->value->type < NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t has_instance =
        neo_js_context_get_constant(ctx)->symbol_has_instance;
    has_instance = neo_js_variable_get_field(another, ctx, has_instance);
    if (has_instance->value->type == NEO_JS_TYPE_EXCEPTION) {
      return has_instance;
    }
    if (has_instance->value->type != NEO_JS_TYPE_FUNCTION) {
      neo_js_variable_t message = neo_js_context_create_cstring(
          ctx, "Right-hand side of 'instanceof' is not an object");
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    return neo_js_variable_call(has_instance, ctx, another, 1, &self);
  }
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_get_false(ctx);
  }
  neo_js_variable_t prototype = neo_js_variable_get_field(
      another, ctx, neo_js_context_create_cstring(ctx, "prototype"));
  if (prototype->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "Function has non-object prototype '%v' in instanceof check",
        prototype);
    neo_js_variable_t type_error =
        neo_js_context_get_constant(ctx)->type_error_class;
    neo_js_variable_t error =
        neo_js_variable_construct(type_error, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_object_t object = (neo_js_object_t)self->value;
  while (object->prototype->type >= NEO_JS_TYPE_OBJECT) {
    neo_js_value_t proto = object->prototype;
    if (proto == prototype->value) {
      return neo_js_context_get_true(ctx);
    }
    object = (neo_js_object_t)object->prototype;
  }
  return neo_js_context_get_false(ctx);
}

void neo_js_variable_gc(neo_allocator_t allocator, neo_list_t variables) {
  neo_list_initialize_t initialize = {true};
  neo_list_t destroyed = neo_create_list(allocator, &initialize);
  neo_js_handle_gc(allocator, variables, destroyed, NULL, NULL);
  neo_allocator_free(allocator, destroyed);
}