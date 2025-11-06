#include "runtime/constant.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "runtime/error.h"
#include "runtime/function.h"
#include "runtime/object.h"
#include "runtime/reference_error.h"
#include "runtime/symbol.h"
#include "runtime/syntax_error.h"
#include "runtime/type_error.h"

void neo_initialize_js_constant(neo_js_context_t ctx) {
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  constant->key_name = neo_js_context_create_cstring(ctx, "name");
  constant->key_constructor = neo_js_context_create_cstring(ctx, "constructor");
  constant->key_prototype = neo_js_context_create_cstring(ctx, "prototype");
  neo_initialize_js_object(ctx);
  neo_initialize_js_function(ctx);
  neo_initialize_js_symbol(ctx);
  neo_initialize_js_error(ctx);
  neo_initialize_js_type_error(ctx);
  neo_initialize_js_syntax_error(ctx);
  neo_initialize_js_reference_error(ctx);
  neo_js_scope_set_variable(root_scope, constant->key_name, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_constructor, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->object_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->object_prototype, NULL);
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