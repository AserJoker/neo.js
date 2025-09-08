#include "engine/basetype/callable.h"
#include "core/allocator.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/string.h"
#include "engine/basetype/object.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <string.h>

neo_js_callable_t neo_js_value_to_callable(neo_js_value_t value) {
  if (value->type->kind >= NEO_JS_TYPE_CFUNCTION) {
    return (neo_js_callable_t)value;
  }
  return NULL;
}

void neo_js_callable_set_closure(neo_js_context_t ctx, neo_js_variable_t self,
                                 const char *name, neo_js_variable_t closure) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  neo_js_chunk_t cvariable = neo_js_variable_get_chunk(self);
  neo_js_handle_t hclosure = neo_js_variable_get_handle(closure);
  neo_js_chunk_t cclosure = neo_js_handle_get_chunk(hclosure);
  neo_js_chunk_add_parent(cclosure, cvariable);
  neo_js_handle_add_ref(hclosure);
  neo_hash_map_set(callable->closure, neo_create_string(allocator, name),
                   hclosure, NULL, NULL);
}

neo_js_variable_t neo_js_callable_get_closure(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              const char *name) {
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  neo_js_handle_t current =
      neo_hash_map_get(callable->closure, name, NULL, NULL);
  return neo_js_context_create_ref_variable(ctx, current, NULL);
}
neo_js_variable_t neo_js_callable_set_bind(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           neo_js_variable_t bind) {
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  if (callable->bind) {
    neo_js_chunk_t root =
        neo_js_scope_get_root_chunk(neo_js_context_get_scope(ctx));
    neo_js_chunk_add_parent(callable->bind, root);
    callable->bind = NULL;
  }
  if (bind) {
    callable->bind = neo_js_variable_get_chunk(bind);
    neo_js_chunk_add_parent(callable->bind, neo_js_variable_get_chunk(self));
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_callable_get_bind(neo_js_context_t ctx,
                                           neo_js_variable_t self) {
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  if (callable->bind) {
    return neo_js_context_create_variable(ctx, callable->bind, NULL);
  }
  return NULL;
}

neo_js_variable_t neo_js_callable_set_class(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            neo_js_variable_t clazz) {
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  if (callable->clazz) {
    neo_js_chunk_t root =
        neo_js_scope_get_root_chunk(neo_js_context_get_scope(ctx));
    neo_js_chunk_add_parent(callable->clazz, root);
    callable->clazz = NULL;
  }
  if (clazz) {
    callable->clazz = neo_js_variable_get_chunk(clazz);
    neo_js_chunk_add_parent(callable->clazz, neo_js_variable_get_chunk(self));
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_callable_get_class(neo_js_context_t ctx,
                                            neo_js_variable_t self) {
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  if (callable->clazz) {
    return neo_js_context_create_variable(ctx, callable->clazz, NULL);
  }
  return NULL;
}
void neo_js_callable_init(neo_allocator_t allocator,
                          neo_js_callable_t callable) {
  neo_js_object_init(allocator, &callable->object);
  callable->bind = NULL;
  callable->clazz = NULL;
  callable->is_class = false;
  callable->name = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = true;
  initialize.auto_free_value = false;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  initialize.compare = (neo_compare_fn_t)strcmp;
  callable->closure = neo_create_hash_map(allocator, &initialize);
}

void neo_js_callable_dispose(neo_allocator_t allocator,
                             neo_js_callable_t callable) {
  for (neo_hash_map_node_t it = neo_hash_map_get_first(callable->closure);
       it != neo_hash_map_get_tail(callable->closure);
       it = neo_hash_map_node_next(it)) {
    neo_js_handle_t handle = neo_hash_map_node_get_value(it);
    if (!neo_js_handle_release(handle)) {
      neo_allocator_free(allocator, handle);
    }
  }
  neo_allocator_free(allocator, callable->closure);
  neo_allocator_free(allocator, callable->name);
  neo_js_object_dispose(allocator, &callable->object);
}