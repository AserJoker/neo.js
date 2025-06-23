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

neo_engine_variable_t
neo_engine_symbol_constructor(neo_engine_context_t ctx,
                              neo_engine_variable_t self, uint32_t argc,
                              neo_engine_variable_t *argv) {
  const wchar_t *description = NULL;
  if (argc > 0) {
    description = neo_engine_variable_to_string(argv[0])->string;
  } else {
    description = L"";
  }
  return neo_engine_context_create_symbol(ctx, description);
}

neo_engine_variable_t neo_engine_symbol_to_string(neo_engine_context_t ctx,
                                                  neo_engine_variable_t self,
                                                  uint32_t argc,
                                                  neo_engine_variable_t *argv) {
  neo_engine_variable_t symbol = self;
  neo_engine_type_t type = neo_engine_variable_get_type(self);
  if (type->kind == NEO_TYPE_OBJECT) {
    if (neo_engine_context_instance_of(
            ctx, symbol, neo_engine_context_get_symbol_constructor(ctx))) {
      symbol = neo_engine_context_to_primitive(ctx, symbol);
      type = neo_engine_variable_get_type(symbol);
    } else {
      return neo_engine_context_create_error(
          ctx, L"TypeError",
          L" Symbol.prototype.toString requires that 'this' be a Symbol");
    }
  }
  if (type->kind == NEO_TYPE_ERROR) {
    return symbol;
  }
  if (type->kind != NEO_TYPE_SYMBOL) {
    return neo_engine_context_create_error(
        ctx, L"TypeError",
        L" Symbol.prototype.toString requires that 'this' be a Symbol");
  }
  neo_allocator_t allocator = neo_engine_context_get_allocator(ctx);
  neo_engine_symbol_t sym = neo_engine_variable_to_symbol(symbol);
  size_t len = wcslen(sym->description) + 8;
  wchar_t *raw = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
  swprintf(raw, len, L"Symbol(%ls)", sym->description);
  neo_engine_variable_t result = neo_engine_context_create_string(ctx, raw);
  neo_allocator_free(allocator, raw);
  return result;
}

neo_engine_variable_t neo_engine_symbol_value_of(neo_engine_context_t ctx,
                                                 neo_engine_variable_t self,
                                                 uint32_t argc,
                                                 neo_engine_variable_t *argv) {
  return neo_engine_symbol_to_primitive(ctx, self, argc, argv);
}

neo_engine_variable_t
neo_engine_symbol_to_primitive(neo_engine_context_t ctx,
                               neo_engine_variable_t self, uint32_t argc,
                               neo_engine_variable_t *argv) {
  neo_engine_type_t type = neo_engine_variable_get_type(self);
  neo_engine_variable_t symbol = self;
  if (type->kind == NEO_TYPE_OBJECT) {
    if (!neo_engine_context_instance_of(
            ctx, symbol, neo_engine_context_get_symbol_constructor(ctx))) {
      return neo_engine_context_create_error(
          ctx, L"TypeError",
          L" Symbol.prototype.toString requires that 'this' be a Symbol");
    } else {
      return neo_engine_context_get_internal(ctx, symbol, L"[[primitive]]");
    }
  }
  if (type->kind != NEO_TYPE_SYMBOL) {
    return neo_engine_context_create_error(
        ctx, L"TypeError",
        L" Symbol.prototype.toString requires that 'this' be a Symbol");
  }
  return symbol;
}

neo_engine_variable_t neo_engine_symbol_for(neo_engine_context_t ctx,
                                            neo_engine_variable_t self,
                                            uint32_t argc,
                                            neo_engine_variable_t *argv) {
  neo_engine_variable_t constructor =
      neo_engine_context_get_symbol_constructor(ctx);
  neo_engine_variable_t registry =
      neo_engine_context_get_internal(ctx, constructor, L"[[registry]]");
  neo_engine_variable_t key = NULL;
  if (argc) {
    key = neo_engine_context_to_string(ctx, argv[0]);
  } else {
    key = neo_engine_context_create_string(ctx, L"");
  }
  if (neo_engine_variable_get_type(registry)->kind != NEO_TYPE_OBJECT) {
    registry = neo_engine_context_create_object(ctx, NULL, NULL);
    neo_engine_context_set_internal(ctx, constructor, L"[[registry]]",
                                    registry);
  }
  neo_engine_variable_t sym = neo_engine_context_get_field(ctx, registry, key);
  if (neo_engine_variable_get_type(sym)->kind != NEO_TYPE_SYMBOL) {
    sym = neo_engine_context_create_symbol(
        ctx, neo_engine_variable_to_string(key)->string);
    neo_engine_context_set_field(ctx, sym, key, sym);
  }
  return sym;
}

neo_engine_variable_t neo_engine_symbol_key_for(neo_engine_context_t ctx,
                                                neo_engine_variable_t self,
                                                uint32_t argc,
                                                neo_engine_variable_t *argv) {
  neo_engine_variable_t constructor =
      neo_engine_context_get_symbol_constructor(ctx);
  neo_engine_variable_t registry =
      neo_engine_context_get_internal(ctx, constructor, L"[[registry]]");
  neo_engine_variable_t sym = NULL;
  if (argc) {
    sym = neo_engine_context_to_string(ctx, argv[0]);
  } else {
    sym = neo_engine_context_create_symbol(ctx, L"");
  }
  if (neo_engine_variable_get_type(registry)->kind != NEO_TYPE_OBJECT) {
    registry = neo_engine_context_create_object(ctx, NULL, NULL);
    neo_engine_context_set_internal(ctx, constructor, L"[[registry]]",
                                    registry);
  }
  neo_engine_object_t object = neo_engine_variable_to_object(registry);
  for (neo_hash_map_node_t it = neo_hash_map_get_first(object->properties);
       it != neo_hash_map_get_tail(object->properties);
       it = neo_hash_map_node_next(it)) {
    neo_engine_handle_t key = neo_hash_map_node_get_key(it);
    neo_engine_handle_t value = neo_hash_map_node_get_value(it);
    if (neo_engine_handle_get_value(value) ==
        neo_engine_variable_get_value(sym)) {
      return neo_engine_context_create_variable(ctx, key);
    }
  }
  return neo_engine_context_create_undefined(ctx);
}