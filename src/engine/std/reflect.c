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
        ctx, NEO_JS_ERROR_TYPE,
        L"Reflect.getOwnPropertyDescriptor called with non-object");
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
        ctx, NEO_JS_ERROR_TYPE, L"Reflect.get called with non-object");
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
        ctx, NEO_JS_ERROR_TYPE,
        L"Reflect.getOwnPropertyDescriptor called with non-object");
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
        ctx, NEO_JS_ERROR_TYPE,
        L"Reflect.getPrototypeOf called with non-object");
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
        ctx, NEO_JS_ERROR_TYPE, L"Reflect.has called with non-object");
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
        ctx, NEO_JS_ERROR_TYPE, L"Reflect.isExtensible called with non-object");
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
        ctx, NEO_JS_ERROR_TYPE, L"Reflect.ownKeys called with non-object");
  }
  neo_js_object_t object = neo_js_variable_to_object(obj);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_hash_map_node_t it = neo_hash_map_get_first(object->properties);
  while (it != neo_hash_map_get_tail(object->properties)) {
    neo_js_chunk_t hkey = neo_hash_map_node_get_key(it);
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
        ctx, NEO_JS_ERROR_TYPE,
        L"Reflect.preventExtensions called with non-object");
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
        ctx, NEO_JS_ERROR_TYPE, L"Reflect.set called with non-object");
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
        ctx, NEO_JS_ERROR_TYPE,
        L"Reflect.setPrototypeOf called with non-object");
  }
  return neo_js_object_set_prototype_of(
      ctx, neo_js_context_get_std(ctx).object_constructor, 1, &obj);
}