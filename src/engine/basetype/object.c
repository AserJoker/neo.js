#include "engine/basetype/object.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "engine/basetype/string.h"
#include "engine/basetype/symbol.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

static const wchar_t *neo_engine_object_typeof(neo_engine_context_t ctx,
                                               neo_engine_variable_t variable) {
  return L"object";
}

static neo_engine_variable_t
neo_engine_object_to_string(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  neo_engine_variable_t to_primitive = neo_engine_context_get_field(
      ctx, neo_engine_context_get_symbol_constructor(ctx),
      neo_engine_context_create_string(ctx, L"toPrimitive"));
  neo_engine_variable_t primitive = NULL;
  if (neo_engine_variable_get_type(to_primitive)->kind == NEO_TYPE_FUNCTION) {
    neo_engine_variable_t hint =
        neo_engine_context_create_string(ctx, L"string");
    primitive = neo_engine_context_call(ctx, to_primitive, self, 1, &hint);
    if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
      return primitive;
    }
  }
  if (!primitive ||
      neo_engine_variable_get_type(primitive)->kind >= NEO_TYPE_OBJECT) {
    neo_engine_variable_t value_of = neo_engine_context_get_field(
        ctx, self, neo_engine_context_create_string(ctx, L"valueOf"));
    if (neo_engine_variable_get_type(value_of)->kind == NEO_TYPE_FUNCTION) {
      primitive = neo_engine_context_call(ctx, value_of, self, 0, NULL);
      if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
        return primitive;
      }
    }
  }
  if (!primitive ||
      neo_engine_variable_get_type(primitive)->kind >= NEO_TYPE_OBJECT) {
    neo_engine_variable_t to_string = neo_engine_context_get_field(
        ctx, self, neo_engine_context_create_string(ctx, L"toString"));
    if (neo_engine_variable_get_type(to_string)->kind == NEO_TYPE_FUNCTION) {
      primitive = neo_engine_context_call(ctx, to_string, self, 0, NULL);
      if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
        return primitive;
      }
    }
  }
  if (!primitive ||
      neo_engine_variable_get_type(primitive)->kind >= NEO_TYPE_OBJECT) {
    return neo_engine_context_create_error(
        ctx, L"TypeError", L"Cannot convert object to primitive value");
  }
  return neo_engine_context_to_string(ctx, primitive);
}

static neo_engine_variable_t
neo_engine_object_to_boolean(neo_engine_context_t ctx,
                             neo_engine_variable_t self) {
  return neo_engine_context_create_boolean(ctx, true);
}
static neo_engine_variable_t
neo_engine_object_to_number(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  neo_engine_variable_t to_primitive = neo_engine_context_get_field(
      ctx, neo_engine_context_get_symbol_constructor(ctx),
      neo_engine_context_create_string(ctx, L"toPrimitive"));
  neo_engine_variable_t primitive = NULL;
  if (neo_engine_variable_get_type(to_primitive)->kind == NEO_TYPE_FUNCTION) {
    neo_engine_variable_t hint =
        neo_engine_context_create_string(ctx, L"number");
    primitive = neo_engine_context_call(ctx, to_primitive, self, 1, &hint);
    if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
      return primitive;
    }
  }
  if (!primitive ||
      neo_engine_variable_get_type(primitive)->kind >= NEO_TYPE_OBJECT) {
    neo_engine_variable_t value_of = neo_engine_context_get_field(
        ctx, self, neo_engine_context_create_string(ctx, L"valueOf"));
    if (neo_engine_variable_get_type(value_of)->kind == NEO_TYPE_FUNCTION) {
      primitive = neo_engine_context_call(ctx, value_of, self, 0, NULL);

      if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
        return primitive;
      }
    }
  }
  if (!primitive ||
      neo_engine_variable_get_type(primitive)->kind >= NEO_TYPE_OBJECT) {
    neo_engine_variable_t to_string = neo_engine_context_get_field(
        ctx, self, neo_engine_context_create_string(ctx, L"to_string"));
    if (neo_engine_variable_get_type(to_string)->kind == NEO_TYPE_FUNCTION) {
      primitive = neo_engine_context_call(ctx, to_string, self, 0, NULL);
      if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
        return primitive;
      }
    }
  }
  if (!primitive ||
      neo_engine_variable_get_type(primitive)->kind != NEO_TYPE_OBJECT) {
    return neo_engine_context_create_error(
        ctx, L"TypeError", L"Cannot convert object to primitive value");
  }
  return neo_engine_context_to_number(ctx, primitive);
}
static neo_engine_variable_t
neo_engine_object_to_primitive(neo_engine_context_t ctx,
                               neo_engine_variable_t self) {
  neo_engine_variable_t to_primitive = neo_engine_context_get_field(
      ctx, neo_engine_context_get_symbol_constructor(ctx),
      neo_engine_context_create_string(ctx, L"toPrimitive"));
  neo_engine_variable_t primitive = NULL;
  if (neo_engine_variable_get_type(to_primitive)->kind == NEO_TYPE_FUNCTION) {
    neo_engine_variable_t hint =
        neo_engine_context_create_string(ctx, L"default");
    primitive = neo_engine_context_call(ctx, to_primitive, self, 1, &hint);
    if (neo_engine_variable_get_type(primitive)->kind < NEO_TYPE_OBJECT) {
      return primitive;
    }
    if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
      return primitive;
    }
  }
  neo_engine_variable_t value_of = neo_engine_context_get_field(
      ctx, self, neo_engine_context_create_string(ctx, L"valueOf"));
  if (neo_engine_variable_get_type(value_of)->kind == NEO_TYPE_FUNCTION) {
    primitive = neo_engine_context_call(ctx, value_of, self, 0, NULL);
    if (neo_engine_variable_get_type(primitive)->kind < NEO_TYPE_OBJECT) {
      return primitive;
    }
    if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
      return primitive;
    }
  }
  neo_engine_variable_t to_string = neo_engine_context_get_field(
      ctx, self, neo_engine_context_create_string(ctx, L"to_string"));
  if (neo_engine_variable_get_type(to_string)->kind == NEO_TYPE_FUNCTION) {
    primitive = neo_engine_context_call(ctx, to_string, self, 0, NULL);
    if (neo_engine_variable_get_type(primitive)->kind < NEO_TYPE_OBJECT) {
      return primitive;
    }
    if (neo_engine_variable_get_type(primitive)->kind == NEO_TYPE_ERROR) {
      return primitive;
    }
  }
  if (!primitive ||
      neo_engine_variable_get_type(primitive)->kind >= NEO_TYPE_OBJECT) {
    return neo_engine_context_create_error(
        ctx, L"TypeError", L"Cannot convert object to primitive value");
  }
  return neo_engine_context_create_error(
      ctx, L"TypeError", L"Cannot convert object to primitive value");
}
static neo_engine_variable_t
neo_engine_object_to_object(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  return self;
}

neo_engine_object_property_t
neo_create_js_object_property(neo_allocator_t allocator) {
  neo_engine_object_property_t property = neo_allocator_alloc(
      allocator, sizeof(struct _neo_engine_object_property_t), NULL);
  property->configurable = false;
  property->enumerable = false;
  property->writable = false;
  property->value = NULL;
  property->get = NULL;
  property->set = NULL;
  return property;
}

neo_engine_object_property_t
neo_engine_object_get_own_property(neo_engine_context_t ctx,
                                   neo_engine_variable_t self,
                                   neo_engine_variable_t field) {
  if (neo_engine_variable_get_type(field)->kind != NEO_TYPE_SYMBOL) {
    field = neo_engine_context_to_string(ctx, field);
  }
  field = neo_engine_context_clone(ctx, field);
  neo_engine_object_t object =
      neo_engine_value_to_object(neo_engine_variable_get_value(self));
  neo_engine_object_property_t property = NULL;
  neo_engine_handle_t hfield = neo_engine_variable_get_handle(field);
  property = neo_hash_map_get(object->properties, hfield, ctx, ctx);
  return property;
}

neo_engine_object_property_t
neo_engine_object_get_property(neo_engine_context_t ctx,
                               neo_engine_variable_t self,
                               neo_engine_variable_t field) {
  if (neo_engine_variable_get_type(field)->kind != NEO_TYPE_SYMBOL) {
    field = neo_engine_context_to_string(ctx, field);
  }
  field = neo_engine_context_clone(ctx, field);
  neo_engine_object_t object =
      neo_engine_value_to_object(neo_engine_variable_get_value(self));
  neo_engine_object_property_t property = NULL;
  neo_engine_handle_t hfield = neo_engine_variable_get_handle(field);
  while (!property && object) {
    property = neo_hash_map_get(object->properties, hfield, ctx, ctx);
    if (property) {
      break;
    }
    object = neo_engine_value_to_object(
        neo_engine_handle_get_value(object->prototype));
  }
  return property;
}

static neo_engine_variable_t
neo_engine_object_get_field(neo_engine_context_t ctx,
                            neo_engine_variable_t self,
                            neo_engine_variable_t field) {
  neo_engine_object_property_t property =
      neo_engine_object_get_property(ctx, self, field);
  if (!property) {
    return neo_engine_context_create_undefined(ctx);
  }
  if (property->get) {
    neo_engine_variable_t getter =
        neo_engine_context_create_variable(ctx, property->get);
    return neo_engine_context_call(ctx, getter, self, 1, NULL);
  } else {
    return neo_engine_context_create_variable(ctx, property->value);
  }
}

static neo_engine_variable_t neo_engine_object_set_field(
    neo_engine_context_t ctx, neo_engine_variable_t self,
    neo_engine_variable_t field, neo_engine_variable_t value) {
  neo_engine_object_property_t proptype =
      neo_engine_object_get_property(ctx, self, field);
  neo_engine_handle_t hobject = neo_engine_variable_get_handle(self);
  neo_engine_object_t object =
      neo_engine_value_to_object(neo_engine_variable_get_value(self));
  neo_engine_handle_t hvalue = neo_engine_variable_get_handle(value);
  if (!proptype) {
    return neo_engine_context_def_field(ctx, self, field, value, true, true,
                                        true);
  } else {
    if (proptype->value) {
      if (!proptype->writable) {
        wchar_t *message = NULL;
        neo_allocator_t allocator = neo_engine_runtime_get_allocator(
            neo_engine_context_get_runtime(ctx));
        if (neo_engine_variable_get_value(field)->type->kind ==
            NEO_TYPE_SYMBOL) {
          neo_engine_symbol_t symbol =
              neo_engine_value_to_symbol(neo_engine_variable_get_value(field));
          size_t len = wcslen(symbol->description) + 64;
          message = neo_allocator_alloc(allocator, len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property Symbol(%ls) of object "
                   L"'#<Object>'",
                   symbol->description);
        } else {
          neo_engine_variable_t sfield =
              neo_engine_context_to_string(ctx, field);
          neo_engine_string_t name =
              neo_engine_value_to_string(neo_engine_variable_get_value(sfield));
          size_t len = wcslen(name->string) + 64;
          message = neo_allocator_alloc(allocator, len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property '%ls' of object "
                   L"'#<Object>'",
                   name->string);
        }
        neo_engine_variable_t error =
            neo_engine_context_create_error(ctx, L"TypeError", message);
        neo_allocator_free(allocator, message);
        return error;
      } else {
        if (proptype->value != hvalue) {
          neo_engine_handle_remove_parent(proptype->value, hobject);
          neo_engine_handle_add_parent(hvalue, hobject);
          neo_engine_scope_t scope = neo_engine_context_get_scope(ctx);
          neo_engine_handle_t root = neo_engine_scope_get_root_handle(scope);
          neo_engine_handle_add_parent(proptype->value, root);
          proptype->value = hvalue;
        }
        return self;
      }
    } else if (proptype->set) {
      neo_engine_variable_t setter =
          neo_engine_context_create_variable(ctx, proptype->set);
      return neo_engine_context_call(ctx, setter, self, 1, &value);
    } else {
      wchar_t *message = NULL;
      neo_allocator_t allocator =
          neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
      if (neo_engine_variable_get_value(field)->type->kind == NEO_TYPE_SYMBOL) {
        neo_engine_symbol_t symbol =
            neo_engine_value_to_symbol(neo_engine_variable_get_value(field));
        size_t len = wcslen(symbol->description) + 64;
        message = neo_allocator_alloc(allocator, len, NULL);
        swprintf(message, len,
                 L"Cannot set property Symbol(%ls) of #<Object> which has only "
                 L"a getter",
                 symbol->description);
      } else {
        neo_engine_variable_t sfield = neo_engine_context_to_string(ctx, field);
        neo_engine_string_t name =
            neo_engine_value_to_string(neo_engine_variable_get_value(sfield));
        size_t len = wcslen(name->string) + 64;
        message = neo_allocator_alloc(allocator, len, NULL);
        swprintf(
            message, len,
            L"Cannot set property '%ls' of #<Object> which has only a getter",
            name->string);
      }
      neo_engine_variable_t error =
          neo_engine_context_create_error(ctx, L"TypeError", message);
      neo_allocator_free(allocator, message);
      return error;
    }
  }
}

static neo_engine_variable_t
neo_engine_object_del_field(neo_engine_context_t ctx,
                            neo_engine_variable_t self,
                            neo_engine_variable_t field) {
  if (neo_engine_variable_get_type(field)->kind != NEO_TYPE_SYMBOL) {
    field = neo_engine_context_to_string(ctx, field);
  }
  field = neo_engine_context_clone(ctx, field);
  neo_engine_object_property_t property =
      neo_engine_object_get_own_property(ctx, self, field);
  neo_engine_object_t object =
      neo_engine_value_to_object(neo_engine_variable_get_value(self));
  if (object->frozen || object->sealed) {
    wchar_t *message = NULL;
    neo_allocator_t allocator =
        neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
    if (neo_engine_variable_get_value(field)->type->kind == NEO_TYPE_SYMBOL) {
      neo_engine_symbol_t symbol =
          neo_engine_value_to_symbol(neo_engine_variable_get_value(field));
      size_t len = wcslen(symbol->description) + 64;
      message = neo_allocator_alloc(allocator, len, NULL);
      swprintf(message, len, L"Cannot delete property Symbol(%ls) of #<Object>",
               symbol->description);
    } else {
      neo_engine_variable_t sfield = neo_engine_context_to_string(ctx, field);
      neo_engine_string_t name =
          neo_engine_value_to_string(neo_engine_variable_get_value(sfield));
      size_t len = wcslen(name->string) + 64;
      message = neo_allocator_alloc(allocator, len, NULL);
      swprintf(message, len, L"Cannot delete property '%ls' of #<Object>",
               name->string);
    }
    neo_engine_variable_t error =
        neo_engine_context_create_error(ctx, L"TypeError", message);
    neo_allocator_free(allocator, message);
    return error;
  }
  if (property) {
    if (!property->configurable) {
      wchar_t *message = NULL;
      neo_allocator_t allocator =
          neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
      if (neo_engine_variable_get_value(field)->type->kind == NEO_TYPE_SYMBOL) {
        neo_engine_symbol_t symbol =
            neo_engine_value_to_symbol(neo_engine_variable_get_value(field));
        size_t len = wcslen(symbol->description) + 64;
        message = neo_allocator_alloc(allocator, len, NULL);
        swprintf(message, len,
                 L"Cannot delete property Symbol(%ls) of #<Object>",
                 symbol->description);
      } else {
        neo_engine_variable_t sfield = neo_engine_context_to_string(ctx, field);
        neo_engine_string_t name =
            neo_engine_value_to_string(neo_engine_variable_get_value(sfield));
        size_t len = wcslen(name->string) + 64;
        message = neo_allocator_alloc(allocator, len, NULL);
        swprintf(message, len, L"Cannot delete property '%ls' of #<Object>",
                 name->string);
      }
      neo_engine_variable_t error =
          neo_engine_context_create_error(ctx, L"TypeError", message);
      neo_allocator_free(allocator, message);
      return error;
    } else {
      neo_hash_map_delete(object->properties,
                          neo_engine_variable_get_handle(field), ctx, ctx);
    }
  }
  return neo_engine_context_create_boolean(ctx, true);
}

static bool neo_engine_object_is_equal(neo_engine_context_t ctx,
                                       neo_engine_variable_t self,
                                       neo_engine_variable_t another) {
  return neo_engine_variable_get_value(self) ==
         neo_engine_variable_get_value(another);
}

static void neo_engine_object_copy(neo_engine_context_t ctx,
                                   neo_engine_variable_t self,
                                   neo_engine_variable_t another) {
  neo_allocator_t allocaotr =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  neo_engine_handle_set_value(allocaotr,
                              neo_engine_variable_get_handle(another),
                              neo_engine_variable_get_value(self));
}

neo_engine_type_t neo_get_js_object_type() {
  static struct _neo_engine_type_t type = {
      NEO_TYPE_OBJECT,
      neo_engine_object_typeof,
      neo_engine_object_to_string,
      neo_engine_object_to_boolean,
      neo_engine_object_to_number,
      neo_engine_object_to_primitive,
      neo_engine_object_to_object,
      neo_engine_object_get_field,
      neo_engine_object_set_field,
      neo_engine_object_del_field,
      neo_engine_object_is_equal,
      neo_engine_object_copy,
  };
  return &type;
}

static void neo_engine_object_dispose(neo_allocator_t allocator,
                                      neo_engine_object_t self) {
  neo_allocator_free(allocator, self->properties);
  neo_allocator_free(allocator, self->internal);
}

int8_t neo_engine_object_compare_key(neo_engine_handle_t handle1,
                                     neo_engine_handle_t handle2,
                                     neo_engine_context_t ctx) {
  neo_engine_variable_t val1 = neo_engine_context_create_variable(ctx, handle1);
  neo_engine_variable_t val2 = neo_engine_context_create_variable(ctx, handle2);
  return neo_engine_context_is_equal(ctx, val1, val2) ? 0 : 1;
}

uint32_t neo_engine_object_key_hash(neo_engine_handle_t handle,
                                    uint32_t max_bucket) {
  neo_engine_value_t value = neo_engine_handle_get_value(handle);
  if (value->type->kind == NEO_TYPE_STRING) {
    return neo_hash_sdb(neo_engine_value_to_string(value)->string, max_bucket);
  } else {
    return (intptr_t)value % max_bucket;
  }
}

neo_engine_object_t neo_create_js_object(neo_allocator_t allocator) {
  neo_engine_object_t object =
      neo_allocator_alloc(allocator, sizeof(struct _neo_engine_object_t),
                          neo_engine_object_dispose);
  object->prototype = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.compare = (neo_compare_fn_t)neo_engine_object_compare_key;
  initialize.hash = (neo_hash_fn_t)neo_engine_object_key_hash;
  initialize.auto_free_value = true;
  object->properties = neo_create_hash_map(allocator, &initialize);
  initialize.auto_free_key = true;
  initialize.compare = (neo_compare_fn_t)wcscmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  object->internal = neo_create_hash_map(allocator, &initialize);
  object->value.ref = 0;
  object->value.type = neo_get_js_object_type();
  object->frozen = false;
  object->extensible = true;
  object->sealed = false;
  object->constructor = NULL;
  return object;
}

neo_engine_object_t neo_engine_value_to_object(neo_engine_value_t value) {
  if (value->type->kind >= NEO_TYPE_OBJECT) {
    return (neo_engine_object_t)value;
  }
  return NULL;
}
