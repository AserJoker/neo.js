#include "runtime/constant.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "runtime/function.h"
#include "runtime/object.h"
#include "runtime/symbol.h"

void neo_initialize_js_constant(neo_js_context_t ctx) {
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  constant->key_name = neo_js_context_create_cstring(ctx, "name");
  constant->key_constructor = neo_js_context_create_cstring(ctx, "constructor");
  constant->key_prototype = neo_js_context_create_cstring(ctx, "prototype");
  neo_js_scope_set_variable(root_scope, constant->key_name, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_constructor, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_prototype, NULL);
  neo_initialize_js_object(ctx);
  neo_initialize_js_function(ctx);
  neo_initialize_js_symbol(ctx);
}