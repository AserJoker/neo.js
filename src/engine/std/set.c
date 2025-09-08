#include "engine/std/set.h"
#include "core/allocator.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/std/array.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <string.h>
#include <wchar.h>

static uint32_t neo_js_set_hash(neo_js_chunk_t hkey, uint32_t max,
                                neo_js_context_t ctx) {
  neo_js_value_t value = neo_js_chunk_get_value(hkey);
  switch (value->type->kind) {
  case NEO_JS_TYPE_NULL:
  case NEO_JS_TYPE_UNDEFINED:
    return 0;
  case NEO_JS_TYPE_NUMBER:
    return (int64_t)neo_js_value_to_number(value)->number % max;
  case NEO_JS_TYPE_STRING:
    return neo_hash_sdb_utf16(neo_js_value_to_string(value)->string, max);
  case NEO_JS_TYPE_BOOLEAN:
    return neo_js_value_to_boolean(value)->boolean ? 1 : 0;
  default:
    return (intptr_t)value % max;
  }
}

static int32_t neo_js_set_cmp(neo_js_chunk_t ha, neo_js_chunk_t hb,
                              neo_js_context_t ctx) {
  if (neo_js_chunk_get_value(ha)->type->kind == NEO_JS_TYPE_NUMBER &&
      neo_js_chunk_get_value(hb)->type->kind == NEO_JS_TYPE_NUMBER) {
    neo_js_number_t na = (neo_js_number_t)neo_js_chunk_get_value(ha);
    neo_js_number_t nb = (neo_js_number_t)neo_js_chunk_get_value(hb);
    if (isnan(na->number) && isnan(nb->number)) {
      return 0;
    }
  }
  neo_js_variable_t a = neo_js_context_create_variable(ctx, ha, NULL);
  neo_js_variable_t b = neo_js_context_create_variable(ctx, hb, NULL);
  if (neo_js_variable_get_type(a) == neo_js_variable_get_type(a)) {
    neo_js_variable_t res = neo_js_context_is_equal(ctx, a, b);
    return neo_js_variable_to_boolean(res)->boolean ? 0 : 1;
  }
  return -1;
}

NEO_JS_CFUNCTION(neo_js_set_constructor) {
  if (neo_js_context_get_call_type(ctx) != NEO_JS_CONSTRUCT_CALL) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "Constructor Set requires 'new'");
  }
  neo_hash_map_initialize_t initialize = {0};
  initialize.hash = (neo_hash_fn_t)neo_js_set_hash;
  initialize.compare = (neo_compare_fn_t)neo_js_set_cmp;
  initialize.auto_free_key = false;
  initialize.auto_free_value = false;
  initialize.max_bucket = 0;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_hash_map_t set = neo_create_hash_map(allocator, &initialize);
  neo_js_context_set_opaque(ctx, self, "#set", set);
  if (argc) {
    neo_js_variable_t arg = argv[0];
    if (neo_js_variable_get_type(arg)->kind == NEO_JS_TYPE_NULL ||
        neo_js_variable_get_type(arg)->kind == NEO_JS_TYPE_UNDEFINED) {
      return self;
    }
    neo_js_variable_t iterator = neo_js_context_get_field(
        ctx, neo_js_context_get_std(ctx).symbol_constructor,
        neo_js_context_create_string(ctx, "iterator"), NULL);
    iterator = neo_js_context_get_field(ctx, arg, iterator, NULL);
    NEO_JS_TRY_AND_THROW(iterator);
    if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
      const char *receiver = neo_js_context_to_error_name(ctx, arg);
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                                "%s is not iterable", receiver);
    }
    iterator = neo_js_context_call(ctx, iterator, arg, 0, NULL);
    NEO_JS_TRY_AND_THROW(iterator);
    if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_OBJECT) {
      const char *receiver = neo_js_context_to_error_name(ctx, arg);
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                                "%s is not iterable", receiver);
    }
    neo_js_variable_t next = neo_js_context_get_field(
        ctx, iterator, neo_js_context_create_string(ctx, "next"), NULL);
    NEO_JS_TRY_AND_THROW(next);
    if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
      const char *receiver = neo_js_context_to_error_name(ctx, arg);
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                                "%s is not iterable", receiver);
    }
    for (;;) {
      neo_js_variable_t res = neo_js_context_call(ctx, next, iterator, 0, NULL);
      NEO_JS_TRY_AND_THROW(res);
      if (neo_js_variable_get_type(res)->kind < NEO_JS_TYPE_OBJECT) {
        const char *receiver = neo_js_context_to_error_name(ctx, arg);
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, 0, "%s is not iterable", receiver);
      }
      neo_js_variable_t done = neo_js_context_get_field(
          ctx, res, neo_js_context_create_string(ctx, "done"), NULL);
      NEO_JS_TRY_AND_THROW(done);
      done = neo_js_context_to_boolean(ctx, done);
      NEO_JS_TRY_AND_THROW(done);
      if (neo_js_variable_to_boolean(done)->boolean) {
        break;
      }
      neo_js_variable_t value = neo_js_context_get_field(
          ctx, res, neo_js_context_create_string(ctx, "value"), NULL);
      NEO_JS_TRY_AND_THROW(value);
      neo_js_set_add(ctx, self, 1, &value);
    }
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_set_species) { return self; }
NEO_JS_CFUNCTION(neo_js_set_add) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.add called on incompatible receiver %s",
        receiver);
  }
  neo_js_variable_t value = NULL;
  if (argc) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_chunk_t chunk = neo_js_variable_get_chunk(value);
  if (!neo_hash_map_has(data, chunk, ctx, ctx)) {
    neo_js_chunk_t self_chunk = neo_js_variable_get_chunk(self);
    neo_js_chunk_add_parent(chunk, self_chunk);
    neo_hash_map_set(data, chunk, chunk, ctx, ctx);
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_set_clear) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.clear called on incompatible receiver %s",
        receiver);
  }
  neo_js_chunk_t self_chunk = neo_js_variable_get_chunk(self);
  neo_hash_map_node_t it = neo_hash_map_get_first(data);
  while (it != neo_hash_map_get_tail(data)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_chunk_remove_parent(chunk, self_chunk);
    neo_js_context_recycle(ctx, chunk);
    it = neo_hash_map_node_next(it);
  }
  neo_hash_map_clear(data);
  return self;
}
NEO_JS_CFUNCTION(neo_js_set_delete) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.delete called on incompatible receiver %s",
        receiver);
  }
  neo_js_chunk_t self_chunk = neo_js_variable_get_chunk(self);
  neo_js_variable_t value = NULL;
  if (argc) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_chunk_t chunk = neo_js_variable_get_chunk(value);
  neo_hash_map_node_t it = neo_hash_map_find(data, chunk, ctx, ctx);
  if (it) {
    chunk = neo_hash_map_node_get_value(it);
    neo_js_chunk_remove_parent(chunk, self_chunk);
    neo_js_context_recycle(ctx, chunk);
    neo_hash_map_erase(data, it);
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_set_difference) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.difference called on incompatible receiver %s",
        receiver);
  }
  neo_js_variable_t another = NULL;
  if (argc) {
    another = argv[0];
  } else {
    another = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).set_constructor, 0, NULL);
  neo_js_variable_t has = neo_js_context_get_field(
      ctx, another, neo_js_context_create_string(ctx, "has"), NULL);
  NEO_JS_TRY_AND_THROW(has);
  if (neo_js_variable_get_type(has)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, another);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, len,
                                              "%s is not set-like", receiver);
  }
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_variable_t res = neo_js_context_call(ctx, another, has, 1, &val);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (!neo_js_variable_to_boolean(res)->boolean) {
      neo_js_set_add(ctx, result, 1, &val);
    }
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_set_entries) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.entries called on incompatible receiver %s",
        receiver);
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_variable_t item = neo_js_context_create_array(ctx);
    NEO_JS_TRY_AND_THROW(neo_js_array_push(ctx, item, 1, &val));
    NEO_JS_TRY_AND_THROW(neo_js_array_push(ctx, item, 1, &val));
    NEO_JS_TRY_AND_THROW(neo_js_array_push(ctx, result, 1, &item));
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_set_for_each) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.forEach called on incompatible receiver %s",
        receiver);
  }
  neo_js_variable_t fn = NULL;
  if (!argc || neo_js_variable_get_type(argv[0])->kind < NEO_JS_TYPE_CALLABLE) {
    const char *name =
        argv[0] ? neo_js_context_to_error_name(ctx, argv[0]) : "undefined";
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              strlen(name) + 32,
                                              "%s is not a function", name);
  }
  fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_variable_t args[] = {val, val, self};
    NEO_JS_TRY_AND_THROW(neo_js_context_call(ctx, fn, this_arg, 3, args));
  }
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_set_has) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.has called on incompatible receiver %s",
        receiver);
  }
  neo_js_chunk_t self_chunk = neo_js_variable_get_chunk(self);
  neo_js_variable_t value = NULL;
  if (argc) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_chunk_t chunk = neo_js_variable_get_chunk(value);
  if (neo_hash_map_find(data, chunk, ctx, ctx)) {
    return neo_js_context_create_boolean(ctx, true);
  }
  return neo_js_context_create_boolean(ctx, false);
}
NEO_JS_CFUNCTION(neo_js_set_intersection) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.intersection called on incompatible receiver "
        "%s",
        receiver);
  }
  neo_js_variable_t another = NULL;
  if (argc) {
    another = argv[0];
  } else {
    another = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).set_constructor, 0, NULL);
  neo_js_variable_t has = neo_js_context_get_field(
      ctx, another, neo_js_context_create_string(ctx, "has"), NULL);
  NEO_JS_TRY_AND_THROW(has);
  if (neo_js_variable_get_type(has)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, another);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, len,
                                              "%s is not set-like", receiver);
  }
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_variable_t res = neo_js_context_call(ctx, another, has, 1, &val);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      neo_js_set_add(ctx, result, 1, &val);
    }
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_set_is_disjoin_form) {

  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.isDisjoinFrom called on incompatible receiver "
        "%s",
        receiver);
  }
  neo_js_variable_t another = NULL;
  if (argc) {
    another = argv[0];
  } else {
    another = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t has = neo_js_context_get_field(
      ctx, another, neo_js_context_create_string(ctx, "has"), NULL);
  NEO_JS_TRY_AND_THROW(has);
  if (neo_js_variable_get_type(has)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, another);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, len,
                                              "%s is not set-like", receiver);
  }
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_variable_t res = neo_js_context_call(ctx, another, has, 1, &val);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      return neo_js_context_create_boolean(ctx, false);
    }
  }
  return neo_js_context_create_boolean(ctx, true);
}
NEO_JS_CFUNCTION(neo_js_set_is_subset_of) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.isSubsetOf called on incompatible receiver %s",
        receiver);
  }
  neo_js_variable_t another = NULL;
  if (argc) {
    another = argv[0];
  } else {
    another = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t has = neo_js_context_get_field(
      ctx, another, neo_js_context_create_string(ctx, "has"), NULL);
  NEO_JS_TRY_AND_THROW(has);
  if (neo_js_variable_get_type(has)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, another);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not set-like", receiver);
  }
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_variable_t res = neo_js_context_call(ctx, another, has, 1, &val);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (!neo_js_variable_to_boolean(res)->boolean) {
      return neo_js_context_create_boolean(ctx, false);
    }
  }
  return neo_js_context_create_boolean(ctx, true);
}
NEO_JS_CFUNCTION(neo_js_set_is_superset_of) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.isSupersetOf called on incompatible receiver "
        "%s",
        receiver);
  }
  neo_js_variable_t another = NULL;
  if (argc) {
    another = argv[0];
  } else {
    another = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t keys = neo_js_context_get_field(
      ctx, another, neo_js_context_create_string(ctx, "keys"), NULL);
  NEO_JS_TRY_AND_THROW(keys);
  if (neo_js_variable_get_type(keys)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, another);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not set-like", receiver);
  }
  keys = neo_js_context_call(ctx, keys, another, 0, NULL);
  NEO_JS_TRY_AND_THROW(keys);
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "iterator"), NULL);
  iterator = neo_js_context_get_field(ctx, keys, iterator, NULL);
  NEO_JS_TRY_AND_THROW(iterator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, keys);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not iterable", receiver);
  }
  iterator = neo_js_context_call(ctx, iterator, keys, 0, NULL);
  NEO_JS_TRY_AND_THROW(iterator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_OBJECT) {
    const char *receiver = neo_js_context_to_error_name(ctx, keys);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not iterable", receiver);
  }
  neo_js_variable_t next = neo_js_context_get_field(
      ctx, iterator, neo_js_context_create_string(ctx, "next"), NULL);
  NEO_JS_TRY_AND_THROW(next);
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, keys);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not iterable", receiver);
  }
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, iterator, 0, NULL);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_get_type(res)->kind < NEO_JS_TYPE_OBJECT) {
      const char *receiver = neo_js_context_to_error_name(ctx, keys);
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                                "%s is not iterable", receiver);
    }
    neo_js_variable_t done = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "done"), NULL);
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t value = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "value"), NULL);
    NEO_JS_TRY_AND_THROW(value);
    neo_js_variable_t check = neo_js_set_has(ctx, self, 1, &value);
    NEO_JS_TRY_AND_THROW(check);
    if (!neo_js_variable_to_boolean(check)->boolean) {
      return neo_js_context_create_boolean(ctx, false);
    }
  }
  return neo_js_context_create_boolean(ctx, true);
}
NEO_JS_CFUNCTION(neo_js_set_keys) {
  return neo_js_set_values(ctx, self, argc, argv);
}
NEO_JS_CFUNCTION(neo_js_set_symmetric_difference) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.symmetricDifference called on incompatible "
        "receiver "
        "%s",
        receiver);
  }
  neo_js_variable_t result = neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).set_constructor, 0, NULL);

  neo_js_variable_t another = NULL;
  if (argc) {
    another = argv[0];
  } else {
    another = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t has = neo_js_context_get_field(
      ctx, another, neo_js_context_create_string(ctx, "has"), NULL);
  NEO_JS_TRY_AND_THROW(has);
  if (neo_js_variable_get_type(has)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, another);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not set-like", receiver);
  }
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_variable_t check = neo_js_context_call(ctx, has, another, 1, &val);
    NEO_JS_TRY_AND_THROW(check);
    check = neo_js_context_to_boolean(ctx, check);
    NEO_JS_TRY_AND_THROW(check);
    if (!neo_js_variable_to_boolean(check)->boolean) {
      neo_js_set_add(ctx, self, 1, &val);
    }
  }

  neo_js_variable_t keys = neo_js_context_get_field(
      ctx, another, neo_js_context_create_string(ctx, "keys"), NULL);
  NEO_JS_TRY_AND_THROW(keys);
  if (neo_js_variable_get_type(keys)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, another);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not set-like", receiver);
  }
  keys = neo_js_context_call(ctx, keys, another, 0, NULL);
  NEO_JS_TRY_AND_THROW(keys);
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "iterator"), NULL);
  iterator = neo_js_context_get_field(ctx, keys, iterator, NULL);
  NEO_JS_TRY_AND_THROW(iterator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, keys);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not iterable", receiver);
  }
  iterator = neo_js_context_call(ctx, iterator, keys, 0, NULL);
  NEO_JS_TRY_AND_THROW(iterator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_OBJECT) {
    const char *receiver = neo_js_context_to_error_name(ctx, keys);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not iterable", receiver);
  }
  neo_js_variable_t next = neo_js_context_get_field(
      ctx, iterator, neo_js_context_create_string(ctx, "next"), NULL);
  NEO_JS_TRY_AND_THROW(next);
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, keys);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not iterable", receiver);
  }
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, iterator, 0, NULL);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_get_type(res)->kind < NEO_JS_TYPE_OBJECT) {
      const char *receiver = neo_js_context_to_error_name(ctx, keys);
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                                "%s is not iterable", receiver);
    }
    neo_js_variable_t done = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "done"), NULL);
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t value = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "value"), NULL);
    NEO_JS_TRY_AND_THROW(value);
    neo_js_variable_t check = neo_js_set_has(ctx, self, 1, &value);
    NEO_JS_TRY_AND_THROW(check);
    if (!neo_js_variable_to_boolean(check)->boolean) {
      neo_js_set_add(ctx, result, 1, &value);
    }
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_set_union) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.symmetricDifference called on incompatible "
        "receiver "
        "%s",
        receiver);
  }
  neo_js_variable_t result = neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).set_constructor, 0, NULL);

  neo_js_variable_t another = NULL;
  if (argc) {
    another = argv[0];
  } else {
    another = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t has = neo_js_context_get_field(
      ctx, another, neo_js_context_create_string(ctx, "has"), NULL);
  NEO_JS_TRY_AND_THROW(has);
  if (neo_js_variable_get_type(has)->kind < NEO_JS_TYPE_CALLABLE) {
    const char *receiver = neo_js_context_to_error_name(ctx, another);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "%s is not set-like", receiver);
  }
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_variable_t check = neo_js_context_call(ctx, has, another, 1, &val);
    NEO_JS_TRY_AND_THROW(check);
    check = neo_js_context_to_boolean(ctx, check);
    NEO_JS_TRY_AND_THROW(check);
    if (!neo_js_variable_to_boolean(check)->boolean) {
      neo_js_set_add(ctx, self, 1, &val);
    }
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_set_values) {
  neo_hash_map_t data = neo_js_context_get_opaque(ctx, self, "#set");
  if (!data) {
    const char *receiver = neo_js_context_to_error_name(ctx, self);
    size_t len = 64 + strlen(receiver);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Method Set.prototype.values called on incompatible receiver %s",
        receiver);
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  for (neo_hash_map_node_t it = neo_hash_map_get_first(data);
       it != neo_hash_map_get_tail(data); it = neo_hash_map_node_next(it)) {
    neo_js_chunk_t chunk = neo_hash_map_node_get_value(it);
    neo_js_variable_t val = neo_js_context_create_variable(ctx, chunk, NULL);
    neo_js_array_push(ctx, result, 1, &val);
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_set_iterator) {
  return neo_js_set_values(ctx, self, argc, argv);
}
void neo_js_context_init_std_set(neo_js_context_t ctx) {
  neo_js_variable_t constructor = neo_js_context_get_std(ctx).set_constructor;

  neo_js_variable_t global = neo_js_context_get_global(ctx);

  neo_js_context_set_field(
      ctx, global, neo_js_context_create_string(ctx, "Set"), constructor, NULL);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, "prototype"), NULL);
  NEO_JS_SET_SYMBOL_METHOD(ctx, constructor, "species", neo_js_set_species);
  NEO_JS_SET_METHOD(ctx, prototype, "add", neo_js_set_add);
  NEO_JS_SET_METHOD(ctx, prototype, "clear", neo_js_set_clear);
  NEO_JS_SET_METHOD(ctx, prototype, "delete", neo_js_set_delete);
  NEO_JS_SET_METHOD(ctx, prototype, "difference", neo_js_set_difference);
  NEO_JS_SET_METHOD(ctx, prototype, "entries", neo_js_set_entries);
  NEO_JS_SET_METHOD(ctx, prototype, "forEach", neo_js_set_for_each);
  NEO_JS_SET_METHOD(ctx, prototype, "has", neo_js_set_has);
  NEO_JS_SET_METHOD(ctx, prototype, "intersection", neo_js_set_intersection);
  NEO_JS_SET_METHOD(ctx, prototype, "isDisjoinFrom",
                    neo_js_set_is_disjoin_form);
  NEO_JS_SET_METHOD(ctx, prototype, "isSubsetOf", neo_js_set_is_subset_of);
  NEO_JS_SET_METHOD(ctx, prototype, "isSuperSetOf", neo_js_set_is_superset_of);
  NEO_JS_SET_METHOD(ctx, prototype, "keys", neo_js_set_keys);
  NEO_JS_SET_METHOD(ctx, prototype, "symmetricDifference",
                    neo_js_set_symmetric_difference);
  NEO_JS_SET_METHOD(ctx, prototype, "union", neo_js_set_union);
  NEO_JS_SET_METHOD(ctx, prototype, "values", neo_js_set_values);
  NEO_JS_SET_SYMBOL_METHOD(ctx, prototype, "iterator", neo_js_set_iterator);
}