#include "engine/std/object.h"
#include "core/allocator.h"
#include "core/hash_map.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/number.h"
#include "engine/basetype/object.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/std/array.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

NEO_JS_CFUNCTION(neo_js_object_assign) {
  if (!argc) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t target = argv[0];
  if (neo_js_variable_get_type(target)->kind < NEO_JS_TYPE_OBJECT) {
    target = neo_js_context_to_object(ctx, target);
    NEO_JS_TRY_AND_THROW(target);
  }
  for (uint32_t idx = 1; idx < argc; idx++) {
    neo_js_variable_t source = argv[idx];
    if (neo_js_variable_get_type(source)->kind == NEO_JS_TYPE_NULL ||
        neo_js_variable_get_type(source)->kind == NEO_JS_TYPE_UNDEFINED) {
      continue;
    }
    source = neo_js_context_to_object(ctx, source);
    NEO_JS_TRY_AND_THROW(source);
    neo_js_object_t obj = neo_js_variable_to_object(source);
    neo_hash_map_node_t it = neo_hash_map_get_first(obj->properties);
    while (it != neo_hash_map_get_tail(obj->properties)) {
      neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
      if (prop->enumerable) {
        neo_js_variable_t field = neo_js_context_create_variable(
            ctx, neo_hash_map_node_get_key(it), NULL);
        neo_js_variable_t val =
            neo_js_context_get_field(ctx, source, field, NULL);
        NEO_JS_TRY_AND_THROW(val);
        NEO_JS_TRY_AND_THROW(
            neo_js_context_set_field(ctx, target, field, val, NULL));
      }
      it = neo_hash_map_node_next(it);
    }
  }
  return target;
}

neo_js_variable_t neo_js_object_create(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  neo_js_variable_t prototype = NULL;
  if (argc > 0) {
    prototype = argv[0];
  }
  return neo_js_context_create_object(ctx, prototype);
}

NEO_JS_CFUNCTION(neo_js_object_define_properties) {
  if (argc < 1 ||
      neo_js_variable_get_type(argv[0])->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Object.defineProperties called on non-object");
  }
  neo_js_variable_t target = argv[0];
  if (argc < 2) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t props = argv[1];
  if (neo_js_variable_get_type(props)->kind < NEO_JS_TYPE_OBJECT) {
    props = neo_js_context_to_object(ctx, props);
    NEO_JS_TRY_AND_THROW(props);
  }
  neo_js_object_t obj = neo_js_variable_to_object(props);
  neo_hash_map_node_t it = neo_hash_map_get_first(obj->properties);
  while (it != neo_hash_map_get_tail(obj->properties)) {
    neo_js_object_property_t p = neo_hash_map_node_get_value(it);
    if (p->enumerable) {
      neo_js_variable_t key = neo_js_context_create_variable(
          ctx, neo_hash_map_node_get_key(it), NULL);
      neo_js_variable_t prop = neo_js_context_get_field(ctx, props, key, NULL);
      NEO_JS_TRY_AND_THROW(prop);
      neo_js_variable_t args[] = {target, key, prop};
      neo_js_variable_t error =
          neo_js_object_define_property(ctx, self, 3, args);
      NEO_JS_TRY_AND_THROW(error);
    }
    it = neo_hash_map_node_next(it);
  }
  return target;
}
NEO_JS_CFUNCTION(neo_js_object_define_property) {
  if (argc < 1 ||
      neo_js_variable_get_type(argv[0])->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Object.defineProperties called on non-object");
  }
  neo_js_variable_t target = argv[0];
  if (argc < 2 || neo_js_variable_get_type(argv[1])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[1])->kind == NEO_JS_TYPE_UNDEFINED) {
    return target;
  }
  neo_js_variable_t key = argv[1];
  if (argc < 3 ||
      neo_js_variable_get_type(argv[2])->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Property description must be an object");
  }
  neo_js_variable_t prop = argv[2];
  if (neo_js_variable_get_type(prop)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Property description must be an object");
  }
  neo_js_variable_t configurable = neo_js_context_get_field(
      ctx, prop, neo_js_context_create_string(ctx, L"configurable"), NULL);
  NEO_JS_TRY_AND_THROW(configurable);
  neo_js_variable_t enumerable = neo_js_context_get_field(
      ctx, prop, neo_js_context_create_string(ctx, L"enumerable"), NULL);
  NEO_JS_TRY_AND_THROW(enumerable);
  configurable = neo_js_context_to_boolean(ctx, configurable);
  NEO_JS_TRY_AND_THROW(configurable);
  enumerable = neo_js_context_to_boolean(ctx, enumerable);
  NEO_JS_TRY_AND_THROW(enumerable);
  neo_js_boolean_t bl_configurable = neo_js_variable_to_boolean(configurable);
  neo_js_boolean_t bl_enumerable = neo_js_variable_to_boolean(enumerable);
  bool has_value = neo_js_context_has_field(
      ctx, prop, neo_js_context_create_string(ctx, L"value"));
  bool has_writable = neo_js_context_has_field(
      ctx, prop, neo_js_context_create_string(ctx, L"writable"));
  bool has_get = neo_js_context_has_field(
      ctx, prop, neo_js_context_create_string(ctx, L"get"));
  bool has_set = neo_js_context_has_field(
      ctx, prop, neo_js_context_create_string(ctx, L"set"));
  if ((has_value || has_writable) && (has_get || has_set)) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Invalid property descriptor. Cannot both specify accessors and a "
        L"value or writable attribute, #<Object>");
  }
  if (has_get || has_set) {
    neo_js_variable_t get = neo_js_context_get_field(
        ctx, prop, neo_js_context_create_string(ctx, L"get"), NULL);
    NEO_JS_TRY_AND_THROW(get);
    neo_js_variable_t set = neo_js_context_get_field(
        ctx, prop, neo_js_context_create_string(ctx, L"set"), NULL);
    NEO_JS_TRY_AND_THROW(set);
    neo_js_variable_t error = neo_js_context_def_accessor(
        ctx, target, key, get, set, bl_configurable->boolean,
        bl_enumerable->boolean);
    NEO_JS_TRY_AND_THROW(error);
  } else {
    neo_js_variable_t value = neo_js_context_get_field(
        ctx, prop, neo_js_context_create_string(ctx, L"value"), NULL);
    NEO_JS_TRY_AND_THROW(value);
    neo_js_variable_t writable = neo_js_context_get_field(
        ctx, prop, neo_js_context_create_string(ctx, L"writable"), NULL);
    NEO_JS_TRY_AND_THROW(writable);
    writable = neo_js_context_to_boolean(ctx, writable);
    NEO_JS_TRY_AND_THROW(writable);
    neo_js_boolean_t bl_writable = neo_js_variable_to_boolean(writable);
    neo_js_variable_t error = neo_js_context_def_field(
        ctx, target, key, value, bl_configurable->boolean,
        bl_enumerable->boolean, bl_writable->boolean);
    NEO_JS_TRY_AND_THROW(error);
  }
  return target;
}
NEO_JS_CFUNCTION(neo_js_object_entries) {
  if (argc < 1 || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_hash_map_node_t it = neo_hash_map_get_first(obj->properties);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  uint32_t idx = 0;
  while (it != neo_hash_map_get_tail(obj->properties)) {
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    neo_js_variable_t key = neo_js_context_create_variable(
        ctx, neo_hash_map_node_get_key(it), NULL);
    if (prop->enumerable &&
        neo_js_variable_get_type(key)->kind == NEO_JS_TYPE_STRING) {
      neo_js_variable_t item = neo_js_context_create_array(ctx);
      neo_js_variable_t error = neo_js_context_set_field(
          ctx, item, neo_js_context_create_number(ctx, 0), key, NULL);
      NEO_JS_TRY_AND_THROW(error);
      neo_js_variable_t value =
          neo_js_context_get_field(ctx, object, key, NULL);
      NEO_JS_TRY_AND_THROW(value);
      error = neo_js_context_set_field(
          ctx, item, neo_js_context_create_number(ctx, 1), value, NULL);
      NEO_JS_TRY_AND_THROW(error);
      error = neo_js_context_set_field(
          ctx, result, neo_js_context_create_number(ctx, idx), item, NULL);
      NEO_JS_TRY_AND_THROW(error);
      idx++;
    }
    it = neo_hash_map_node_next(it);
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_object_freeze) {
  if (!argc) {
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t object = argv[0];
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    return object;
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  obj->frozen = true;
  return object;
}
NEO_JS_CFUNCTION(neo_js_object_from_entries) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"variable is not iterable");
  }
  neo_js_variable_t entries = argv[0];
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_js_variable_t iterator_symbol = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, L"iterator"), NULL);
  neo_js_variable_t iterator =
      neo_js_context_get_field(ctx, entries, iterator_symbol, NULL);
  NEO_JS_TRY_AND_THROW(iterator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"variable is not iterable");
  }
  neo_js_variable_t generator =
      neo_js_context_call(ctx, iterator, entries, 0, NULL);
  NEO_JS_TRY_AND_THROW(generator);
  neo_js_variable_t next = neo_js_context_get_field(
      ctx, generator, neo_js_context_create_string(ctx, L"next"), NULL);
  NEO_JS_TRY_AND_THROW(next);
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"variable is not iterable");
  }
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, generator, 0, NULL);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_get_type(res)->kind < NEO_JS_TYPE_OBJECT) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                                L"variable is not iterable");
    }
    neo_js_variable_t done = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, L"done"), NULL);
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t value = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, L"value"), NULL);
    NEO_JS_TRY_AND_THROW(value);
    neo_js_variable_t key = neo_js_context_get_field(
        ctx, value, neo_js_context_create_number(ctx, 0), NULL);
    NEO_JS_TRY_AND_THROW(key);
    neo_js_variable_t val = neo_js_context_get_field(
        ctx, value, neo_js_context_create_number(ctx, 1), NULL);
    NEO_JS_TRY_AND_THROW(val);
    neo_js_variable_t error =
        neo_js_context_set_field(ctx, result, key, val, NULL);
    NEO_JS_TRY_AND_THROW(error);
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_object_get_own_property_descriptor) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_variable_t key = NULL;
  if (argc < 2) {
    key = neo_js_context_create_undefined(ctx);
  } else {
    key = argv[1];
  }
  neo_js_object_property_t prop = neo_js_object_get_property(ctx, object, key);
  if (!prop) {
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_js_context_set_field(
      ctx, result, neo_js_context_create_string(ctx, L"configurable"),
      neo_js_context_create_boolean(ctx, prop->configurable), NULL);
  neo_js_context_set_field(
      ctx, result, neo_js_context_create_string(ctx, L"enumerable"),
      neo_js_context_create_boolean(ctx, prop->enumerable), NULL);
  if (prop->value) {
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"value"),
        neo_js_context_create_variable(ctx, prop->value, NULL), NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"writable"),
        neo_js_context_create_boolean(ctx, prop->writable), NULL);
  } else {
    if (prop->get) {
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"get"),
          neo_js_context_create_variable(ctx, prop->get, NULL), NULL);
    } else {
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, L"get"),
                               neo_js_context_create_undefined(ctx), NULL);
    }
    if (prop->set) {
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"set"),
          neo_js_context_create_variable(ctx, prop->set, NULL), NULL);
    } else {
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, L"set"),
                               neo_js_context_create_undefined(ctx), NULL);
    }
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_object_get_own_property_descriptors) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_hash_map_node_t it = neo_hash_map_get_first(obj->properties);
  while (it != neo_hash_map_get_tail(obj->properties)) {
    neo_js_variable_t key = neo_js_context_create_variable(
        ctx, neo_hash_map_node_get_key(it), NULL);
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    neo_js_variable_t item = neo_js_context_create_object(ctx, NULL);
    neo_js_variable_t error = neo_js_context_set_field(
        ctx, item, neo_js_context_create_string(ctx, L"configurable"),
        neo_js_context_create_boolean(ctx, prop->configurable), NULL);
    NEO_JS_TRY_AND_THROW(error);
    error = neo_js_context_set_field(
        ctx, item, neo_js_context_create_string(ctx, L"enumerable"),
        neo_js_context_create_boolean(ctx, prop->enumerable), NULL);
    NEO_JS_TRY_AND_THROW(error);
    if (prop->value) {
      error = neo_js_context_set_field(
          ctx, item, neo_js_context_create_string(ctx, L"writable"),
          neo_js_context_create_boolean(ctx, prop->writable), NULL);
      NEO_JS_TRY_AND_THROW(error);
      error = neo_js_context_set_field(
          ctx, item, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, prop->value, NULL), NULL);
      NEO_JS_TRY_AND_THROW(error);
    } else {
      error = neo_js_context_set_field(
          ctx, item, neo_js_context_create_string(ctx, L"get"),
          neo_js_context_create_variable(ctx, prop->get, NULL), NULL);
      NEO_JS_TRY_AND_THROW(error);
      error = neo_js_context_set_field(
          ctx, item, neo_js_context_create_string(ctx, L"set"),
          neo_js_context_create_variable(ctx, prop->set, NULL), NULL);
      NEO_JS_TRY_AND_THROW(error);
    }
    error = neo_js_context_set_field(ctx, result, key, item, NULL);
    NEO_JS_TRY_AND_THROW(error);
    it = neo_hash_map_node_next(it);
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_object_get_own_property_names) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  uint32_t idx = 0;
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_hash_map_node_t it = neo_hash_map_get_first(obj->properties);
  while (it != neo_hash_map_get_tail(obj->properties)) {
    neo_js_variable_t key = neo_js_context_create_variable(
        ctx, neo_hash_map_node_get_key(it), NULL);
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    if (prop->enumerable &&
        neo_js_variable_get_type(key)->kind != NEO_JS_TYPE_SYMBOL) {
      neo_js_variable_t error = neo_js_context_set_field(
          ctx, result, neo_js_context_create_number(ctx, idx), key, NULL);
      NEO_JS_TRY_AND_THROW(error);
      idx++;
    }
    it = neo_hash_map_node_next(it);
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_object_get_own_property_symbols) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  uint32_t idx = 0;
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_hash_map_node_t it = neo_hash_map_get_first(obj->properties);
  while (it != neo_hash_map_get_tail(obj->properties)) {
    neo_js_variable_t key = neo_js_context_create_variable(
        ctx, neo_hash_map_node_get_key(it), NULL);
    neo_js_object_property_t prop = neo_hash_map_node_get_value(it);
    if (prop->enumerable &&
        neo_js_variable_get_type(key)->kind == NEO_JS_TYPE_SYMBOL) {
      neo_js_variable_t error = neo_js_context_set_field(
          ctx, result, neo_js_context_create_number(ctx, idx), key, NULL);
      NEO_JS_TRY_AND_THROW(error);
      idx++;
    }
    it = neo_hash_map_node_next(it);
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_object_get_prototype_of) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  return neo_js_context_create_variable(ctx, obj->prototype, NULL);
}

NEO_JS_CFUNCTION(neo_js_object_group_by) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Object.groupBy called on null or undefined");
  }
  neo_js_variable_t object = argv[0];
  if (argc < 2 ||
      neo_js_variable_get_type(argv[1])->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"variable is not a function");
  }
  neo_js_variable_t callbackfn = argv[1];
  neo_js_variable_t iterator_symbol = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, L"iterator"), NULL);
  neo_js_variable_t iterator =
      neo_js_context_get_field(ctx, object, iterator_symbol, NULL);
  NEO_JS_TRY_AND_THROW(iterator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"variable is not iterable");
  }
  neo_js_variable_t generator =
      neo_js_context_call(ctx, iterator, object, 0, NULL);
  NEO_JS_TRY_AND_THROW(generator);
  neo_js_variable_t next = neo_js_context_get_field(
      ctx, generator, neo_js_context_create_string(ctx, L"next"), NULL);
  NEO_JS_TRY_AND_THROW(next);
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"variable is not iterable");
  }
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, generator, 0, NULL);
    NEO_JS_TRY_AND_THROW(res);
    neo_js_variable_t done = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, L"done"), NULL);
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t value = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, L"value"), NULL);
    NEO_JS_TRY_AND_THROW(value);
    neo_js_variable_t group_name = neo_js_context_call(
        ctx, callbackfn, neo_js_context_create_undefined(ctx), 1, &value);
    neo_js_variable_t group = NULL;
    if (!neo_js_context_has_field(ctx, res, group_name)) {
      group = neo_js_context_create_array(ctx);
      NEO_JS_TRY_AND_THROW(
          neo_js_context_set_field(ctx, res, group_name, group, NULL));
    } else {
      group = neo_js_context_get_field(ctx, res, group_name, NULL);
      NEO_JS_TRY_AND_THROW(group);
    }
    neo_js_array_push(ctx, group, 1, &value);
  }
  return result;
}

NEO_JS_CFUNCTION(neo_js_object_has_own) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_js_variable_t prop = NULL;
  if (argc < 2) {
    prop = neo_js_context_create_undefined(ctx);
  } else {
    prop = argv[1];
  }
  neo_hash_map_node_t it = neo_hash_map_get_first(obj->properties);
  while (it != neo_hash_map_get_tail(obj->properties)) {
    neo_js_variable_t key = neo_js_context_create_variable(
        ctx, neo_hash_map_node_get_key(it), NULL);
    neo_js_variable_t is_equal = neo_js_context_is_equal(ctx, key, prop);
    NEO_JS_TRY_AND_THROW(is_equal);
    if (neo_js_variable_to_boolean(is_equal)->boolean) {
      return is_equal;
    }
    it = neo_hash_map_node_next(it);
  }
  return neo_js_context_create_boolean(ctx, false);
}
NEO_JS_CFUNCTION(neo_js_object_is) {
  neo_js_variable_t value1 = NULL;
  if (argc < 1) {
    value1 = neo_js_context_create_undefined(ctx);
  } else {
    value1 = argv[0];
  }
  neo_js_variable_t value2 = NULL;
  if (argc < 2) {
    value2 = neo_js_context_create_undefined(ctx);
  } else {
    value2 = argv[1];
  }
  if (neo_js_variable_get_type(value1) != neo_js_variable_get_type(value2)) {
    return neo_js_context_create_boolean(ctx, false);
  }
  if (neo_js_variable_get_type(value1)->kind == NEO_JS_TYPE_NUMBER) {
    neo_js_number_t num1 = neo_js_variable_to_number(value1);
    neo_js_number_t num2 = neo_js_variable_to_number(value2);
    if (isnan(num1->number) && isnan(num2->number)) {
      return neo_js_context_create_boolean(ctx, true);
    }
    if (num1->number == -0.f && num2->number == 0.f) {
      return neo_js_context_create_boolean(ctx, false);
    }
    if (num1->number == 0.f && num2->number == -0.f) {
      return neo_js_context_create_boolean(ctx, false);
    }
    if (num1->number == -0.f && num2->number == -0.f) {
      return neo_js_context_create_boolean(ctx, true);
    }
    if (num1->number == 0.f && num2->number == 0.f) {
      return neo_js_context_create_boolean(ctx, true);
    }
    return neo_js_context_create_boolean(ctx, num1->number == num2->number);
  } else {
    return neo_js_context_is_equal(ctx, value1, value2);
  }
}
NEO_JS_CFUNCTION(neo_js_object_is_extensible) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  return neo_js_context_create_boolean(ctx, obj->extensible);
}
NEO_JS_CFUNCTION(neo_js_object_is_frozen) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_boolean(ctx, true);
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  return neo_js_context_create_boolean(ctx, obj->frozen);
}
NEO_JS_CFUNCTION(neo_js_object_is_sealed) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_boolean(ctx, true);
  }
  neo_js_variable_t object = argv[0];
  object = neo_js_context_to_object(ctx, object);
  NEO_JS_TRY_AND_THROW(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  return neo_js_context_create_boolean(ctx, obj->sealed);
}

NEO_JS_CFUNCTION(neo_js_object_keys) {
  if (argc < 1) {
    return neo_js_context_create_array(ctx);
  }
  return neo_js_context_get_keys(ctx, argv[0]);
}

NEO_JS_CFUNCTION(neo_js_object_prevent_extensions) {
  if (!argc) {
    return neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(argv[0])->kind < NEO_JS_TYPE_OBJECT) {
    return argv[0];
  }
  neo_js_variable_t object = argv[0];
  neo_js_object_t obj = neo_js_variable_to_object(object);
  obj->extensible = false;
  return object;
}
NEO_JS_CFUNCTION(neo_js_object_seal) {
  if (!argc) {
    return neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(argv[0])->kind < NEO_JS_TYPE_OBJECT) {
    return argv[0];
  }
  neo_js_variable_t object = argv[0];
  neo_js_object_t obj = neo_js_variable_to_object(object);
  obj->sealed = true;
  return object;
}

NEO_JS_CFUNCTION(neo_js_object_set_prototype_of) {
  if (!argc || neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Object.setPrototypeOf called on null or undefined");
  }
  neo_js_variable_t object = argv[0];
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    return object;
  }
  if (argc < 2 ||
      (neo_js_variable_get_type(argv[1])->kind != NEO_JS_TYPE_NULL &&
       neo_js_variable_get_type(argv[1])->kind != NEO_JS_TYPE_OBJECT)) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Object prototype may only be an Object or null");
  }
  neo_js_variable_t prop = argv[1];
  neo_js_variable_t error = neo_js_object_set_prototype(ctx, object, prop);
  NEO_JS_TRY_AND_THROW(error);
  return object;
}

NEO_JS_CFUNCTION(neo_js_object_values) {
  neo_js_variable_t object = NULL;
  neo_js_variable_t keys = neo_js_object_keys(ctx, object, 0, NULL);
  NEO_JS_TRY_AND_THROW(keys);
  neo_js_variable_t len = neo_js_context_get_field(
      ctx, keys, neo_js_context_create_string(ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(len);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  for (uint32_t idx = 0; idx < num->number; idx++) {
    neo_js_variable_t vidx = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_context_get_field(ctx, keys, vidx, NULL);
    neo_js_variable_t value = neo_js_context_get_field(ctx, object, key, NULL);
    neo_js_variable_t error =
        neo_js_context_set_field(ctx, result, vidx, value, NULL);
    NEO_JS_TRY_AND_THROW(error);
  }
  return result;
}

neo_js_variable_t neo_js_object_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_object(ctx, NULL);
  }
  if (argc > 0) {
    return neo_js_context_to_object(ctx, argv[0]);
  }
  return self;
}

neo_js_variable_t neo_js_object_value_of(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  return self;
}

neo_js_variable_t neo_js_object_to_string(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_variable_t toStringTag = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, L"toStringTag"), NULL);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t tag =
      neo_js_context_get_field(ctx, self, toStringTag, NULL);
  tag = neo_js_context_to_primitive(ctx, tag, L"default");
  if (neo_js_variable_get_type(tag)->kind == NEO_JS_TYPE_STRING) {
    size_t len = wcslen(neo_js_variable_to_string(tag)->string);
    len += 16;
    wchar_t *msg = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
    swprintf(msg, len, L"[object %ls]", neo_js_variable_to_string(tag)->string);
    neo_js_variable_t str = neo_js_context_create_string(ctx, msg);
    neo_allocator_free(allocator, msg);
    return str;
  } else {
    return neo_js_context_create_string(ctx, L"[object Object]");
  }
}
neo_js_variable_t neo_js_object_has_own_property(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 uint32_t argc,
                                                 neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_boolean(ctx, false);
  }
  self = neo_js_context_to_object(ctx, self);
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(ctx, self, argv[0]);
  return neo_js_context_create_boolean(ctx, prop != NULL);
}

neo_js_variable_t neo_js_object_is_prototype_of(neo_js_context_t ctx,
                                                neo_js_variable_t self,
                                                uint32_t argc,
                                                neo_js_variable_t *argv) {
  if (argc < 1) {
    return neo_js_context_create_boolean(ctx, false);
  }
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_object_t obj = neo_js_variable_to_object(self);
  while (obj) {
    if (neo_js_handle_get_value(obj->prototype) ==
        neo_js_variable_get_value(argv[0])) {
      return neo_js_context_create_boolean(ctx, true);
    }
    obj = neo_js_value_to_object(neo_js_handle_get_value(obj->prototype));
  }
  return neo_js_context_create_boolean(ctx, false);
}

neo_js_variable_t
neo_js_object_property_is_enumerable(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT || argc < 1) {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_object_property_t prop =
      neo_js_object_get_property(ctx, self, argv[0]);
  if (!prop) {
    return neo_js_context_create_boolean(ctx, false);
  }
  return neo_js_context_create_boolean(ctx, prop->enumerable);
}

neo_js_variable_t neo_js_object_to_local_string(neo_js_context_t ctx,
                                                neo_js_variable_t self,
                                                uint32_t argc,
                                                neo_js_variable_t *argv) {
  return neo_js_object_to_string(ctx, self, argc, argv);
}