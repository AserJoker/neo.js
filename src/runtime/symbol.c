#include "runtime/symbol.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/string.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/string.h"
#include "engine/symbol.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DEF_SYMBOL(constant, name)                                             \
  do {                                                                         \
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, #name);         \
    const uint16_t *description = ((neo_js_string_t)key->value)->value;        \
    constant = neo_js_context_create_symbol(ctx, description);                 \
    neo_js_variable_def_field(clazz, ctx, key, constant, true, false, true);   \
  } while (0)

NEO_JS_CFUNCTION(neo_js_symbol_for) {
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t clazz = constant->symbol_class;
  neo_hash_map_t symbols = neo_js_variable_get_opaque(clazz, ctx, "symbols");
  neo_js_variable_t key = NULL;
  if (argc) {
    key = argv[0];
  } else {
    key = neo_js_context_create_undefined(ctx);
  }
  key = neo_js_variable_to_string(key, ctx);
  if (key->value->type == NEO_JS_TYPE_EXCEPTION) {
    return key;
  }
  const uint16_t *keystring = ((neo_js_string_t)key->value)->value;
  neo_js_value_t value = neo_hash_map_get(symbols, keystring, NULL, NULL);
  if (value) {
    return neo_js_context_create_variable(ctx, value);
  }
  neo_js_variable_t symbol = neo_js_context_create_symbol(ctx, keystring);
  neo_js_value_add_parent(symbol->value, clazz->value);
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_hash_map_set(symbols, neo_create_string16(allocator, keystring),
                   symbol->value, NULL, NULL);
  return symbol;
}
NEO_JS_CFUNCTION(neo_js_symbol_key_for) {
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t clazz = constant->symbol_class;
  neo_hash_map_t symbols = neo_js_variable_get_opaque(clazz, ctx, "symbols");
  neo_js_variable_t symbol = NULL;
  if (argc) {
    symbol = argv[0];
  } else {
    symbol = neo_js_context_create_undefined(ctx);
  }
  if (symbol->value->type != NEO_JS_TYPE_SYMBOL) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a symbol", symbol);
    // TODO: message -> error
    neo_js_variable_t error = message;
    return neo_js_context_create_exception(ctx, error);
  }
  neo_hash_map_node_t it = neo_hash_map_get_first(symbols);
  while (it != neo_hash_map_get_tail(symbols)) {
    const uint16_t *key = neo_hash_map_node_get_key(it);
    neo_js_value_t value = neo_hash_map_node_get_value(it);
    if (value == symbol->value) {
      return neo_js_context_create_string(ctx, key);
    }
    it = neo_hash_map_node_next(it);
  }
  return neo_js_context_create_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_symbol_constructor) {
  const uint16_t *desc = NULL;
  if (argc) {
    neo_js_variable_t description = NULL;
    description = neo_js_variable_to_string(argv[0], ctx);
    if (description->value->type == NEO_JS_TYPE_EXCEPTION) {
      return description;
    }
    desc = ((neo_js_string_t)description->value)->value;
  }
  return neo_js_context_create_symbol(ctx, desc);
}
NEO_JS_CFUNCTION(neo_js_symbol_value_of) {
  if (self->value->type != NEO_JS_TYPE_SYMBOL) {
    if (self->value->type < NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Symbol.prototype.valueOf requires that 'this' be a Symbol");
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    self = neo_js_variable_get_internel(self, ctx, "PrimitiveValue");
    if (!self || self->value->type != NEO_JS_TYPE_SYMBOL) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Symbol.prototype.valueOf requires that 'this' be a Symbol");
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_symbol_to_string) {
  if (self->value->type != NEO_JS_TYPE_SYMBOL) {
    if (self->value->type < NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Symbol.prototype.toString requires that 'this' be a Symbol");
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    self = neo_js_variable_get_internel(self, ctx, "PrimitiveValue");
    if (!self || self->value->type != NEO_JS_TYPE_SYMBOL) {
      neo_js_variable_t message = neo_js_context_format(
          ctx, "Symbol.prototype.toString requires that 'this' be a Symbol");
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
  }
  neo_js_symbol_t symbol = (neo_js_symbol_t)self->value;
  size_t len = neo_string16_length(symbol->description);
  uint16_t string[len + 9];
  const char *prefix = "Symbol(";
  uint16_t *dst = &string[0];
  while (*prefix) {
    *dst++ = *prefix++;
  }
  const uint16_t *src = symbol->description;
  while (*src) {
    *dst++ = *src++;
  }
  *dst++ = ')';
  *dst = 0;
  return neo_js_context_create_string(ctx, string);
}
NEO_JS_CFUNCTION(neo_js_symbol_symbol_to_primitive) {
  if (self->value->type != NEO_JS_TYPE_SYMBOL) {
    if (self->value->type < NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Symbol.prototype[Symbol.toPrimitive] "
                                     "requires that 'this' be a Symbol");
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
    self = neo_js_variable_get_internel(self, ctx, "PrimitiveValue");
    if (!self || self->value->type != NEO_JS_TYPE_SYMBOL) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Symbol.prototype[Symbol.toPrimitive] "
                                     "requires that 'this' be a Symbol");
      // TODO: message -> error
      neo_js_variable_t error = message;
      return neo_js_context_create_exception(ctx, error);
    }
  }
  return self;
}
void neo_initialize_js_symbol(neo_js_context_t ctx) {
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  constant->symbol_class =
      neo_js_context_create_cfunction(ctx, neo_js_symbol_constructor, "Symbol");
  constant->symbol_prototype = neo_js_variable_get_field(
      constant->symbol_class, ctx, constant->key_prototype);
  neo_js_variable_t clazz = constant->symbol_class;
  NEO_JS_DEF_METHOD(ctx, clazz, "for", neo_js_symbol_for);
  NEO_JS_DEF_METHOD(ctx, clazz, "keyFor", neo_js_symbol_key_for);
  neo_js_variable_t prototype = constant->symbol_prototype;
  NEO_JS_DEF_METHOD(ctx, prototype, "valueOf", neo_js_symbol_value_of);
  NEO_JS_DEF_METHOD(ctx, prototype, "toString", neo_js_symbol_to_string);
  DEF_SYMBOL(constant->symbol_async_dispose, asyncDispose);
  DEF_SYMBOL(constant->symbol_async_iterator, asyncIterator);
  DEF_SYMBOL(constant->symbol_iterator, iterator);
  DEF_SYMBOL(constant->symbol_match, match);
  DEF_SYMBOL(constant->symbol_match_all, matchAll);
  DEF_SYMBOL(constant->symbol_replace, replace);
  DEF_SYMBOL(constant->symbol_search, search);
  DEF_SYMBOL(constant->symbol_species, species);
  DEF_SYMBOL(constant->symbol_split, split);
  DEF_SYMBOL(constant->symbol_to_primitive, toPrimitive);
  DEF_SYMBOL(constant->symbol_to_string_tag, toStringTag);
  NEO_DEF_SYMBOL_METHOD(ctx, prototype, constant->symbol_to_primitive,
                        "toPrimitive", neo_js_symbol_symbol_to_primitive);
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = true;
  initialize.auto_free_value = false;
  initialize.compare = (neo_compare_fn_t)neo_string16_compare;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb_utf16;
  neo_hash_map_t symbols = neo_create_hash_map(allocator, &initialize);
  neo_js_variable_set_opaque(clazz, ctx, "symbols", symbols);
  neo_js_variable_t string_tag = neo_js_context_create_cstring(ctx, "Symbol");
  neo_js_variable_def_field(prototype, ctx, constant->symbol_to_string_tag,
                            string_tag, true, false, true);
  neo_js_scope_set_variable(root_scope, constant->symbol_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_async_dispose, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_async_iterator, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_iterator, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_match, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_match_all, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_replace, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_search, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_species, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_split, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_to_primitive, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_to_string_tag, NULL);
}