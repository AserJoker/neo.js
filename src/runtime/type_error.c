#include "runtime/type_error.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include "runtime/error.h"
NEO_JS_CFUNCTION(neo_js_type_error_constructor) {
  return neo_js_error_constructor(ctx, self, argc, argv);
}
void neo_initialize_js_type_error(neo_js_context_t ctx) {
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  constant->type_error_class = neo_js_context_create_cfunction(
      ctx, neo_js_type_error_constructor, "TypeError");
  neo_js_variable_t prototype = neo_js_variable_get_field(
      constant->type_error_class, ctx, constant->key_prototype);
  neo_js_variable_t string = neo_js_context_create_cstring(ctx, "TypeError");
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "name");
  neo_js_variable_def_field(prototype, ctx, key, string, true, false, true);
  neo_js_scope_set_variable(root_scope, constant->type_error_class, NULL);
}