#include "engine/std/reflect.h"
#include "core/hash_map.h"
#include "engine/context.h"
#include "engine/std/array.h"
#include "engine/std/function.h"
#include "engine/std/object.h"
#include "engine/type.h"
#include "engine/variable.h"
NEO_JS_CFUNCTION(neo_js_reflect_apply) {
  neo_js_variable_t func = NULL;
  if (argc) {
    func = argv[0];
  } else {
    func = neo_js_context_create_undefined(ctx);
  }
  return neo_js_function_apply(ctx, func, argc - 1, &argv[1]);
}
NEO_JS_CFUNCTION(neo_js_reflect_construct) {
  neo_js_variable_t func = NULL;
  if (argc) {
    func = argv[0];
  } else {
    func = neo_js_context_create_undefined(ctx);
  }
  return neo_js_context_construct(ctx, func, argc - 1, &argv[1]);
}
NEO_JS_CFUNCTION(neo_js_reflect_define_property) {
  return neo_js_object_define_property(
      ctx, neo_js_context_get_std(ctx).object_constructor, argc, argv);
}
NEO_JS_CFUNCTION(neo_js_reflect_delete_property) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Reflect.getOwnPropertyDescriptor called with non-object");
  }
  neo_js_variable_t key = NULL;
  if (argc > 1) {
    key = argv[1];
  } else {
    key = neo_js_context_create_undefined(ctx);
  }
  return neo_js_context_del_field(ctx, obj, key);
}
NEO_JS_CFUNCTION(neo_js_reflect_get) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t field = NULL;
  if (argc > 1) {
    field = argv[1];
  } else {
    field = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t receiver = NULL;
  if (argc > 2) {
    receiver = argv[2];
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Reflect.get called with non-object");
  }
  return neo_js_context_get_field(ctx, obj, field, receiver);
}
NEO_JS_CFUNCTION(neo_js_reflect_get_own_property_descriptor) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Reflect.getOwnPropertyDescriptor called with non-object");
  }
  return neo_js_object_get_own_property_descriptor(
      ctx, neo_js_context_get_std(ctx).object_constructor, argc, argv);
}
NEO_JS_CFUNCTION(neo_js_reflect_get_prototype_of) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Reflect.getPrototypeOf called with non-object");
  }
  return neo_js_object_get_prototype_of(
      ctx, neo_js_context_get_std(ctx).object_constructor, 1, &obj);
}
NEO_JS_CFUNCTION(neo_js_reflect_has) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t field = NULL;
  if (argc > 1) {
    field = argv[1];
  } else {
    field = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Reflect.has called with non-object");
  }
  return neo_js_context_in(ctx, field, obj);
}
NEO_JS_CFUNCTION(neo_js_reflect_is_extensible) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Reflect.isExtensible called with non-object");
  }
  return neo_js_object_is_extensible(
      ctx, neo_js_context_get_std(ctx).object_constructor, 1, &obj);
}
NEO_JS_CFUNCTION(neo_js_reflect_own_keys) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Reflect.ownKeys called with non-object");
  }
  neo_js_object_t object = neo_js_variable_to_object(obj);
  if (neo_js_variable_get_type(obj)->kind >= NEO_JS_TYPE_OBJECT &&
      neo_js_context_has_internal(ctx, obj, "[[target]]") &&
      neo_js_context_has_internal(ctx, obj, "[[handler]]")) {
    neo_js_variable_t target =
        neo_js_context_get_internal(ctx, obj, "[[target]]");
    neo_js_variable_t handler =
        neo_js_context_get_internal(ctx, obj, "[[handler]]");
    if (neo_js_context_has_field(
            ctx, handler, neo_js_context_create_string(ctx, "ownKeys"))) {
      neo_js_variable_t ownKeys =
          neo_js_context_get_string_field(ctx, handler, "ownKeys");
      NEO_JS_TRY_AND_THROW(ownKeys);
      neo_js_variable_t args[] = {target};
      return neo_js_context_call(ctx, ownKeys,
                                 neo_js_context_create_undefined(ctx), 1, args);
    }
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_hash_map_node_t it = neo_hash_map_get_first(object->properties);
  while (it != neo_hash_map_get_tail(object->properties)) {
    neo_js_handle_t hkey = neo_hash_map_node_get_key(it);
    neo_js_variable_t key = neo_js_context_create_variable(ctx, hkey, NULL);
    neo_js_array_push(ctx, result, 1, &key);
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_reflect_prevent_extensions) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Reflect.preventExtensions called with non-object");
  }
  return neo_js_object_prevent_extensions(
      ctx, neo_js_context_get_std(ctx).object_constructor, 1, &obj);
}
NEO_JS_CFUNCTION(neo_js_reflect_set) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t field = NULL;
  if (argc > 1) {
    field = argv[1];
  } else {
    field = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t value = NULL;
  if (argc > 2) {
    value = argv[2];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t receiver = NULL;
  if (argc > 3) {
    receiver = argv[3];
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Reflect.set called with non-object");
  }
  return neo_js_context_set_field(ctx, obj, field, value, receiver);
}
NEO_JS_CFUNCTION(neo_js_reflect_set_prototype_of) {
  neo_js_variable_t obj = NULL;
  if (argc) {
    obj = argv[0];
  } else {
    obj = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(obj)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Reflect.setPrototypeOf called with non-object");
  }
  return neo_js_object_set_prototype_of(
      ctx, neo_js_context_get_std(ctx).object_constructor, 1, &obj);
}

void neo_js_context_init_std_reflect(neo_js_context_t ctx) {
  neo_js_variable_t reflect =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));
  neo_js_context_set_field(ctx, neo_js_context_get_std(ctx).global,
                           neo_js_context_create_string(ctx, "Reflect"),
                           reflect, NULL);
  neo_js_context_set_field(
      ctx, reflect,
      neo_js_context_get_string_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor, "toStringTag"),
      neo_js_context_create_string(ctx, "Reflect"), NULL);
  neo_js_variable_t apply =
      neo_js_context_create_cfunction(ctx, "apply", neo_js_reflect_apply);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "apply"), apply,
                           true, false, true);
  neo_js_variable_t construct = neo_js_context_create_cfunction(
      ctx, "construct", neo_js_reflect_construct);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "construct"),
                           construct, true, false, true);
  neo_js_variable_t define_property = neo_js_context_create_cfunction(
      ctx, "defineProperty", neo_js_reflect_define_property);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "defineProperty"),
                           define_property, true, false, true);
  neo_js_variable_t delete_property = neo_js_context_create_cfunction(
      ctx, "deleteProperty", neo_js_reflect_delete_property);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "deleteProperty"),
                           delete_property, true, false, true);
  neo_js_variable_t get =
      neo_js_context_create_cfunction(ctx, "get", neo_js_reflect_get);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "get"), get, true,
                           false, true);
  neo_js_variable_t get_own_property_descriptor =
      neo_js_context_create_cfunction(
          ctx, "getOwnPropertyDescriptor",
          neo_js_reflect_get_own_property_descriptor);
  neo_js_context_def_field(
      ctx, reflect,
      neo_js_context_create_string(ctx, "getOwnPropertyDescriptor"),
      get_own_property_descriptor, true, false, true);
  neo_js_variable_t get_prototype_of = neo_js_context_create_cfunction(
      ctx, "getPrototypeOf", neo_js_reflect_get_prototype_of);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "getPrototypeOf"),
                           get_prototype_of, true, false, true);
  neo_js_variable_t has =
      neo_js_context_create_cfunction(ctx, "has", neo_js_reflect_has);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "has"), has, true,
                           false, true);
  neo_js_variable_t is_extensible = neo_js_context_create_cfunction(
      ctx, "isExtensible", neo_js_reflect_is_extensible);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "isExtensible"),
                           is_extensible, true, false, true);
  neo_js_variable_t own_keys =
      neo_js_context_create_cfunction(ctx, "ownKeys", neo_js_reflect_own_keys);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "ownKeys"),
                           own_keys, true, false, true);
  neo_js_variable_t prevent_extensions = neo_js_context_create_cfunction(
      ctx, "preventExtensions", neo_js_reflect_prevent_extensions);
  neo_js_context_def_field(
      ctx, reflect, neo_js_context_create_string(ctx, "preventExtensions"),
      prevent_extensions, true, false, true);
  neo_js_variable_t set =
      neo_js_context_create_cfunction(ctx, "set", neo_js_reflect_set);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "set"), set, true,
                           false, true);
  neo_js_variable_t set_prototype_of = neo_js_context_create_cfunction(
      ctx, "setPrototypeOf", neo_js_reflect_set_prototype_of);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, "setPrototypeOf"),
                           set_prototype_of, true, false, true);
}