#include "engine/std/console.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
neo_js_variable_t neo_js_console_constructor(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_console_log(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  for (uint32_t idx = 0; idx < argc; idx++) {
    if (idx != 0) {
      printf(",");
    }
    neo_js_variable_t arg = argv[idx];
    if (neo_js_variable_get_type(arg)->kind == NEO_JS_TYPE_SYMBOL) {
      neo_js_symbol_t sym = neo_js_variable_to_symbol(arg);
      printf("Symbol(%ls)", sym->description);
    } else if (neo_js_variable_get_type(arg)->kind == NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t to_string = neo_js_context_get_field(
          ctx, arg, neo_js_context_create_string(ctx, L"toString"));
      NEO_JS_TRY_AND_THROW(to_string);
      neo_js_variable_t res = neo_js_context_call(ctx, to_string, arg, 0, NULL);
      NEO_JS_TRY_AND_THROW(res);
      res = neo_js_context_to_string(ctx, res);
      NEO_JS_TRY_AND_THROW(res);
      printf("%ls", neo_js_variable_to_string(res)->string);
    } else {
      arg = neo_js_context_to_string(ctx, arg);
      neo_js_string_t str = neo_js_variable_to_string(arg);
      printf("%ls", str->string);
    }
  }
  printf("\n");
  return neo_js_context_create_undefined(ctx);
}