#include "engine/basetype/callable.h"
#include "core/allocator.h"
#include "core/hash_map.h"
#include "core/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"

neo_js_callable_t neo_js_value_to_callable(neo_js_value_t value) {
  if (value->type->kind >= NEO_TYPE_CFUNCTION) {
    return (neo_js_callable_t)value;
  }
  return NULL;
}


void neo_js_callable_set_closure(neo_js_context_t ctx, neo_js_variable_t self,
                                 const wchar_t *name,
                                 neo_js_variable_t closure) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  neo_js_handle_t hvariable = neo_js_variable_get_handle(self);
  neo_js_handle_t hclosure = neo_js_variable_get_handle(closure);
  neo_js_handle_add_parent(hclosure, hvariable);
  neo_js_handle_t current =
      neo_hash_map_get(callable->closure, name, NULL, NULL);
  if (current) {
    neo_js_handle_remove_parent(current, hvariable);
  }
  neo_hash_map_set(callable->closure, neo_create_wstring(allocator, name),
                   hclosure, NULL, NULL);
}

neo_js_variable_t neo_js_callable_get_closure(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              const wchar_t *name) {
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  neo_js_handle_t hvariable = neo_js_variable_get_handle(self);
  neo_js_handle_t current =
      neo_hash_map_get(callable->closure, name, NULL, NULL);
  return neo_js_context_create_variable(ctx, current, NULL);
}