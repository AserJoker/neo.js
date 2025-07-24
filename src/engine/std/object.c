#include "engine/std/object.h"
#include "core/allocator.h"
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
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_object(ctx, NULL);
  }
  if (argc > 0) {
    return neo_js_context_to_object(ctx, argv[0]);
  }
  return self;
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

neo_js_variable_t neo_js_object_keys(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {

  if (argc < 1) {
    return neo_js_context_create_array(ctx);
  }
  return neo_js_context_get_keys(ctx, argv[0]);
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
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t tag = neo_js_context_get_field(ctx, self, toStringTag);
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