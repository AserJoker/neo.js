#include "engine/std/object.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "engine/basetype/object.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stddef.h>
#include <wchar.h>
neo_js_variable_t neo_js_object_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(self)->kind < NEO_TYPE_OBJECT) {
    return neo_js_context_create_object(ctx, NULL, NULL);
  }
  if (argc > 0) {
    return neo_js_context_to_object(ctx, argv[0]);
  }
  return self;
}

neo_js_variable_t neo_js_object_keys(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {

  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t result = neo_js_context_create_array(ctx, 0);
  if (argc < 1) {
    return result;
  }
  neo_js_variable_t obj = argv[1];
  obj = neo_js_context_to_object(ctx, obj);
  neo_js_object_t object = neo_js_variable_to_object(obj);
  neo_hash_map_initialize_t initialize;
  initialize.auto_free_key = false;
  initialize.auto_free_value = false;
  initialize.compare = (neo_compare_fn_t)wcscmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  neo_hash_map_t cache = neo_create_hash_map(allocator, &initialize);
  size_t idx = 0;
  while (object) {
    for (neo_hash_map_node_t it = neo_hash_map_get_first(object->properties);
         it != neo_hash_map_get_tail(object->properties);
         it = neo_hash_map_node_next(it)) {
      neo_js_handle_t key = neo_hash_map_node_get_key(it);
      neo_js_string_t string =
          neo_js_value_to_string(neo_js_handle_get_value(key));
      if (!neo_hash_map_has(cache, string->string, NULL, NULL)) {
        neo_js_context_set_field(
            ctx, result, neo_js_context_create_number(ctx, idx),
            neo_js_context_create_string(ctx, string->string));
        idx++;
        neo_hash_map_set(cache, string->string, key, NULL, NULL);
      }
    }
    object = neo_js_value_to_object(neo_js_handle_get_value(object->prototype));
  }
  neo_allocator_free(allocator, cache);
  return result;
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
      ctx, neo_js_context_get_symbol_constructor(ctx),
      neo_js_context_create_string(ctx, L"toStringTag"));

  neo_js_variable_t tag = neo_js_context_get_field(ctx, self, toStringTag);
  tag = neo_js_context_to_primitive(ctx, tag);
  if (neo_js_variable_get_type(tag)->kind == NEO_TYPE_STRING) {
    wchar_t msg[16];
    swprintf(msg, 16, L"[object %s]", neo_js_variable_to_string(tag)->string);
    return neo_js_context_create_string(ctx, msg);
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
  neo_js_object_t obj = neo_js_variable_to_object(self);
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
  if (neo_js_variable_get_type(self)->kind < NEO_TYPE_OBJECT) {
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
  if (neo_js_variable_get_type(self)->kind < NEO_TYPE_OBJECT || argc < 1) {
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