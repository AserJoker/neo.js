#include "engine/std/symbol.h"
#include "core/allocator.h"
#include "core/hash_map.h"
#include "engine/basetype/object.h"
#include "engine/basetype/symbol.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <wchar.h>

neo_js_variable_t neo_js_symbol_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  if (neo_js_context_get_call_type(ctx) == NEO_JS_CONSTRUCT_CALL) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"Symbol is not a constructor");
  }
  const wchar_t *description = NULL;
  if (argc > 0) {
    neo_js_variable_t str = argv[0];
    str = neo_js_context_to_string(ctx, str);
    if (neo_js_variable_get_type(str)->kind == NEO_JS_TYPE_ERROR) {
      return str;
    }
    description = neo_js_variable_to_string(str)->string;
  } else {
    description = L"";
  }
  return neo_js_context_create_symbol(ctx, description);
}

neo_js_variable_t neo_js_symbol_to_string(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_variable_t symbol = self;
  neo_js_type_t type = neo_js_variable_get_type(self);
  if (type->kind == NEO_JS_TYPE_OBJECT) {
    if (neo_js_context_instance_of(
            ctx, symbol, neo_js_context_get_std(ctx).symbol_constructor)) {
      symbol = neo_js_context_to_primitive(ctx, symbol, L"default");
      type = neo_js_variable_get_type(symbol);
    } else {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE,
          L" Symbol.prototype.toString requires that 'this' be a Symbol");
    }
  }
  if (type->kind == NEO_JS_TYPE_ERROR) {
    return symbol;
  }
  if (type->kind != NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L" Symbol.prototype.toString requires that 'this' be a Symbol");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_symbol_t sym = neo_js_variable_to_symbol(symbol);
  size_t len = wcslen(sym->description) + 16;
  wchar_t *raw = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
  swprintf(raw, len, L"Symbol(%ls)", sym->description);
  neo_js_variable_t result = neo_js_context_create_string(ctx, raw);
  neo_allocator_free(allocator, raw);
  return result;
}

neo_js_variable_t neo_js_symbol_value_of(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  return neo_js_symbol_to_primitive(ctx, self, argc, argv);
}

neo_js_variable_t neo_js_symbol_to_primitive(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t symbol = self;
  if (type->kind == NEO_JS_TYPE_OBJECT) {
    if (!neo_js_context_instance_of(
            ctx, symbol, neo_js_context_get_std(ctx).symbol_constructor)) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE,
          L" Symbol.prototype.toString requires that 'this' be a Symbol");
    } else {
      return neo_js_context_get_internal(ctx, symbol, L"[[primitive]]");
    }
  }
  if (type->kind != NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L" Symbol.prototype.toString requires that 'this' be a Symbol");
  }
  return symbol;
}

neo_js_variable_t neo_js_symbol_for(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  neo_js_variable_t constructor =
      neo_js_context_get_std(ctx).symbol_constructor;
  neo_js_variable_t registry =
      neo_js_context_get_internal(ctx, constructor, L"[[registry]]");
  neo_js_variable_t key = NULL;
  if (argc) {
    key = neo_js_context_to_string(ctx, argv[0]);
  } else {
    key = neo_js_context_create_string(ctx, L"");
  }
  if (neo_js_variable_get_type(registry)->kind != NEO_JS_TYPE_OBJECT) {
    registry = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_internal(ctx, constructor, L"[[registry]]", registry);
  }
  neo_js_variable_t sym = neo_js_context_get_field(ctx, registry, key);
  if (neo_js_variable_get_type(sym)->kind != NEO_JS_TYPE_SYMBOL) {
    sym = neo_js_context_create_symbol(ctx,
                                       neo_js_variable_to_string(key)->string);
    neo_js_context_set_field(ctx, sym, key, sym);
  }
  return sym;
}

neo_js_variable_t neo_js_symbol_key_for(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_variable_t constructor =
      neo_js_context_get_std(ctx).symbol_constructor;
  neo_js_variable_t registry =
      neo_js_context_get_internal(ctx, constructor, L"[[registry]]");
  neo_js_variable_t sym = NULL;
  if (argc) {
    sym = neo_js_context_to_string(ctx, argv[0]);
  } else {
    sym = neo_js_context_create_symbol(ctx, L"");
  }
  if (neo_js_variable_get_type(registry)->kind != NEO_JS_TYPE_OBJECT) {
    registry = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_internal(ctx, constructor, L"[[registry]]", registry);
  }
  neo_js_object_t object = neo_js_variable_to_object(registry);
  for (neo_hash_map_node_t it = neo_hash_map_get_first(object->properties);
       it != neo_hash_map_get_tail(object->properties);
       it = neo_hash_map_node_next(it)) {
    neo_js_handle_t key = neo_hash_map_node_get_key(it);
    neo_js_handle_t value = neo_hash_map_node_get_value(it);
    if (neo_js_handle_get_value(value) == neo_js_variable_get_value(sym)) {
      return neo_js_context_create_variable(ctx, key, NULL);
    }
  }
  return neo_js_context_create_undefined(ctx);
}