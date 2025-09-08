#include "engine/std/console.h"
#include "engine/context.h"
#include "engine/std/object.h"
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
      printf("Symbol(%s)", sym->description);
    } else if (neo_js_variable_get_type(arg)->kind == NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t to_string = neo_js_context_get_field(
          ctx, arg, neo_js_context_create_string(ctx, "toString"), NULL);
      NEO_JS_TRY_AND_THROW(to_string);

      neo_js_variable_t res = NULL;
      if (neo_js_variable_get_type(to_string)->kind < NEO_JS_TYPE_CALLABLE) {
        res = neo_js_object_to_string(ctx, arg, 0, NULL);
      } else {
        res = neo_js_context_call(ctx, to_string, arg, 0, NULL);
      }
      NEO_JS_TRY_AND_THROW(res);
      res = neo_js_context_to_string(ctx, res);
      NEO_JS_TRY_AND_THROW(res);
      printf("%s", neo_js_variable_to_string(res)->string);
    } else {
      arg = neo_js_context_to_string(ctx, arg);
      neo_js_string_t str = neo_js_variable_to_string(arg);
      printf("%s", str->string);
    }
  }
  printf("\n");
  return neo_js_context_create_undefined(ctx);
}

void neo_js_context_init_std_console(neo_js_context_t ctx) {
  neo_js_variable_t console_constructor = neo_js_context_create_cfunction(
      ctx, "Console", neo_js_console_constructor);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, console_constructor, neo_js_context_create_string(ctx, "prototype"),
      NULL);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "log"),
      neo_js_context_create_cfunction(ctx, "log", neo_js_console_log), true,
      false, true);

  neo_js_variable_t console =
      neo_js_context_construct(ctx, console_constructor, 0, NULL);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).global,
                           neo_js_context_create_string(ctx, "console"),
                           console, true, true, true);
}