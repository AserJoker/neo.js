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
    neo_js_variable_t arg = argv[idx];
    if (neo_js_variable_get_type(arg)->kind == NEO_TYPE_SYMBOL) {
      neo_js_symbol_t sym = neo_js_variable_to_symbol(arg);
      printf("Symbol(%ls)\n", sym->description);
    } else {
      arg = neo_js_context_to_string(ctx, arg);
      neo_js_string_t str = neo_js_variable_to_string(arg);
      printf("%ls\n", str->string);
    }
  }
  return neo_js_context_create_undefined(ctx);
}