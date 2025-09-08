#include "engine/std/symbol.h"
#include "core/allocator.h"
#include "core/hash_map.h"
#include "engine/basetype/object.h"
#include "engine/basetype/symbol.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <string.h>
#include <wchar.h>

neo_js_variable_t neo_js_symbol_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  if (neo_js_context_get_call_type(ctx) == NEO_JS_CONSTRUCT_CALL) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "Symbol is not a constructor");
  }
  const char *description = NULL;
  if (argc > 0) {
    neo_js_variable_t str = argv[0];
    str = neo_js_context_to_string(ctx, str);
    if (neo_js_variable_get_type(str)->kind == NEO_JS_TYPE_ERROR) {
      return str;
    }
    description = neo_js_context_to_cstring(ctx, str);
  } else {
    description = "";
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
      symbol = neo_js_context_to_primitive(ctx, symbol, "default");
      type = neo_js_variable_get_type(symbol);
    } else {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          " Symbol.prototype.toString requires that 'this' be a Symbol");
    }
  }
  if (type->kind == NEO_JS_TYPE_ERROR) {
    return symbol;
  }
  if (type->kind != NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        " Symbol.prototype.toString requires that 'this' be a Symbol");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_symbol_t sym = neo_js_variable_to_symbol(symbol);
  size_t len = strlen(sym->description) + 16;
  char *raw = neo_allocator_alloc(allocator, sizeof(char) * len, NULL);
  snprintf(raw, len, "Symbol(%s)", sym->description);
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
          ctx, NEO_JS_ERROR_TYPE, 0,
          " Symbol.prototype.toString requires that 'this' be a Symbol");
    } else {
      return neo_js_context_get_internal(ctx, symbol, "[[primitive]]");
    }
  }
  if (type->kind != NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        " Symbol.prototype.toString requires that 'this' be a Symbol");
  }
  return symbol;
}

neo_js_variable_t neo_js_symbol_for(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  neo_js_variable_t constructor =
      neo_js_context_get_std(ctx).symbol_constructor;
  neo_js_variable_t registry =
      neo_js_context_get_internal(ctx, constructor, "[[registry]]");
  neo_js_variable_t key = NULL;
  if (argc) {
    key = neo_js_context_to_string(ctx, argv[0]);
  } else {
    key = neo_js_context_create_string(ctx, "");
  }
  if (neo_js_variable_get_type(registry)->kind != NEO_JS_TYPE_OBJECT) {
    registry = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_internal(ctx, constructor, "[[registry]]", registry);
  }
  neo_js_variable_t sym = neo_js_context_get_field(ctx, registry, key, NULL);
  if (neo_js_variable_get_type(sym)->kind != NEO_JS_TYPE_SYMBOL) {
    sym =
        neo_js_context_create_symbol(ctx, neo_js_context_to_cstring(ctx, key));
    neo_js_context_set_field(ctx, sym, key, sym, NULL);
  }
  return sym;
}

neo_js_variable_t neo_js_symbol_key_for(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_variable_t constructor =
      neo_js_context_get_std(ctx).symbol_constructor;
  neo_js_variable_t registry =
      neo_js_context_get_internal(ctx, constructor, "[[registry]]");
  neo_js_variable_t sym = NULL;
  if (argc) {
    sym = neo_js_context_to_string(ctx, argv[0]);
  } else {
    sym = neo_js_context_create_symbol(ctx, "");
  }
  if (neo_js_variable_get_type(registry)->kind != NEO_JS_TYPE_OBJECT) {
    registry = neo_js_context_create_object(ctx, NULL);
    neo_js_context_set_internal(ctx, constructor, "[[registry]]", registry);
  }
  neo_js_object_t object = neo_js_variable_to_object(registry);
  for (neo_hash_map_node_t it = neo_hash_map_get_first(object->properties);
       it != neo_hash_map_get_tail(object->properties);
       it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t key = neo_hash_map_node_get_key(it);
    neo_js_chunk_t value = neo_hash_map_node_get_value(it);
    if (neo_js_chunk_get_value(value) == neo_js_variable_get_value(sym)) {
      return neo_js_context_create_variable(ctx, key, NULL);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

void neo_js_context_init_std_symbol(neo_js_context_t ctx) {
  neo_js_context_push_scope(ctx);
  neo_js_variable_t async_iterator =
      neo_js_context_create_symbol(ctx, "asyncIterator");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "asyncIterator"),
                           async_iterator, true, false, true);

  neo_js_variable_t async_dispose =
      neo_js_context_create_symbol(ctx, "asyncDispose");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "asyncDispose"),
                           async_dispose, true, false, true);

  neo_js_variable_t dispose = neo_js_context_create_symbol(ctx, "dispose");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "dispose"),
                           dispose, true, false, true);

  neo_js_variable_t has_instance =
      neo_js_context_create_symbol(ctx, "hasInstance");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "hasInstance"),
                           has_instance, true, false, true);

  neo_js_variable_t is_concat_spreadable =
      neo_js_context_create_symbol(ctx, "isConcatSpreadable");
  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "isConcatSpreadable"),
      is_concat_spreadable, true, false, true);

  neo_js_variable_t iterator = neo_js_context_create_symbol(ctx, "iterator");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "iterator"),
                           iterator, true, false, true);

  neo_js_variable_t match = neo_js_context_create_symbol(ctx, "match");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "match"), match,
                           true, false, true);

  neo_js_variable_t match_all = neo_js_context_create_symbol(ctx, "matchAll");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "matchAll"),
                           match_all, true, false, true);

  neo_js_variable_t replace = neo_js_context_create_symbol(ctx, "replace");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "replace"),
                           replace, true, false, true);

  neo_js_variable_t search = neo_js_context_create_symbol(ctx, "search");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "search"), search,
                           true, false, true);

  neo_js_variable_t species = neo_js_context_create_symbol(ctx, "species");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "species"),
                           species, true, false, true);

  neo_js_variable_t split = neo_js_context_create_symbol(ctx, "split");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "split"), split,
                           true, false, true);

  neo_js_variable_t to_primitive =
      neo_js_context_create_symbol(ctx, "toPrimitive");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "toPrimitive"),
                           to_primitive, true, false, true);

  neo_js_variable_t to_string_tag =
      neo_js_context_create_symbol(ctx, "toStringTag");
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "toStringTag"),
                           to_string_tag, true, false, true);

  neo_js_variable_t for_ =
      neo_js_context_create_cfunction(ctx, "for", &neo_js_symbol_for);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "for"), for_, true,
                           false, true);

  neo_js_variable_t key_for =
      neo_js_context_create_cfunction(ctx, "keyFor", &neo_js_symbol_key_for);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).symbol_constructor,
                           neo_js_context_create_string(ctx, "keyFor"), key_for,
                           true, false, true);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL);

  neo_js_variable_t to_string = neo_js_context_create_cfunction(
      ctx, "toString", &neo_js_symbol_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "toString"),
                           to_string, true, false, true);

  neo_js_variable_t value_of =
      neo_js_context_create_cfunction(ctx, "valueOf", &neo_js_symbol_value_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "valueOf"),
                           value_of, true, false, true);

  neo_js_variable_t to_primitive_func = neo_js_context_create_cfunction(
      ctx, "[Symbol.toPrimitive]", &neo_js_symbol_to_primitive);

  neo_js_context_def_field(ctx, prototype, to_primitive, to_primitive_func,
                           true, false, true);
  neo_js_context_pop_scope(ctx);
}