#include "neojs/runtime/console.h"
#include "neojs/core/allocator.h"
#include "neojs/core/string.h"
#include "neojs/engine/context.h"
#include "neojs/engine/string.h"
#include "neojs/engine/symbol.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
#include <stdint.h>
#include <stdio.h>

NEO_JS_CFUNCTION(neo_js_console_constructor) { return self; }
NEO_JS_CFUNCTION(neo_js_console_log) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  for (size_t idx = 0; idx < argc; idx++) {
    if (idx != 0) {
      printf(" ,");
    }
    neo_js_variable_t arg = argv[idx];
    if (arg->value->type == NEO_JS_TYPE_SYMBOL) {
      neo_js_symbol_t symbol = (neo_js_symbol_t)arg->value;
      char *description =
          neo_string16_to_string(allocator, symbol->description);
      printf("Symbol(%s)", description);
      neo_allocator_free(allocator, description);
    } else {
      if (arg->value->type != NEO_JS_TYPE_STRING) {
        arg = neo_js_variable_to_string(arg, ctx);
      }
      neo_js_string_t str = (neo_js_string_t)arg->value;
      char *s = neo_string16_to_string(allocator, str->value);
      printf("%s", s);
      neo_allocator_free(allocator, s);
    }
  }
  printf("\n");
  return neo_js_context_get_undefined(ctx);
}
void neo_initialize_js_console(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t console = neo_js_context_create_cfunction(
      ctx, neo_js_console_constructor, "Console");
  neo_js_variable_t prototype =
      neo_js_variable_get_field(console, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, prototype, "log", neo_js_console_log);
  constant->console = neo_js_variable_construct(console, ctx, 0, NULL);
}