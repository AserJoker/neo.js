#include "runtime/constant.h"
#include "engine/boolean.h"
#include "engine/context.h"
#include "engine/null.h"
#include "engine/number.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/undefined.h"
#include "engine/uninitialized.h"
#include "runtime/array.h"
#include "runtime/error.h"
#include "runtime/function.h"
#include "runtime/object.h"
#include "runtime/reference_error.h"
#include "runtime/symbol.h"
#include "runtime/syntax_error.h"
#include "runtime/type_error.h"
#include <math.h>

void neo_initialize_js_constant(neo_js_context_t ctx) {
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  constant->uninitialized = neo_js_context_create_variable(
      ctx, &neo_create_js_uninitialized(allocator)->super);
  constant->undefined = neo_js_context_create_variable(
      ctx, &neo_create_js_undefined(allocator)->super);
  constant->null = neo_js_context_create_variable(
      ctx, &neo_create_js_null(allocator)->super);
  constant->nan = neo_js_context_create_variable(
      ctx, &neo_create_js_number(allocator, NAN)->super);
  constant->infinity = neo_js_context_create_variable(
      ctx, &neo_create_js_number(allocator, INFINITY)->super);
  constant->boolean_true = neo_js_context_create_variable(
      ctx, &neo_create_js_boolean(allocator, true)->super);
  constant->boolean_false = neo_js_context_create_variable(
      ctx, &neo_create_js_boolean(allocator, false)->super);
  constant->key_name = neo_js_context_create_cstring(ctx, "name");
  constant->key_constructor = neo_js_context_create_cstring(ctx, "constructor");
  constant->key_prototype = neo_js_context_create_cstring(ctx, "prototype");
  neo_initialize_js_object(ctx);
  neo_initialize_js_function(ctx);
  neo_initialize_js_symbol(ctx);
  neo_initialize_js_array(ctx);
  neo_initialize_js_error(ctx);
  neo_initialize_js_type_error(ctx);
  neo_initialize_js_syntax_error(ctx);
  neo_initialize_js_reference_error(ctx);

  neo_js_scope_set_variable(root_scope, constant->uninitialized, NULL);
  neo_js_scope_set_variable(root_scope, constant->undefined, NULL);
  neo_js_scope_set_variable(root_scope, constant->null, NULL);
  neo_js_scope_set_variable(root_scope, constant->nan, NULL);
  neo_js_scope_set_variable(root_scope, constant->infinity, NULL);
  neo_js_scope_set_variable(root_scope, constant->boolean_true, NULL);
  neo_js_scope_set_variable(root_scope, constant->boolean_false, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_name, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_constructor, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->object_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->object_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->array_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->array_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->function_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->function_class, NULL);
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
  neo_js_scope_set_variable(root_scope, constant->error_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->type_error_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->syntax_error_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->reference_error_class, NULL);
}