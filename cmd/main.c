
#include "core/allocator.h"
#include "core/error.h"
#include "js/basetype/string.h"
#include "js/context.h"
#include "js/runtime.h"
#include "js/type.h"
#include "js/variable.h"
#include <locale.h>
#include <stdio.h>

static neo_js_variable_t js_println(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t string = neo_js_context_to_string(ctx, argv[idx]);
    neo_js_string_t str = neo_js_variable_to_string(string);
    printf("%ls", str->string);
    if (idx != argc - 1) {
      printf(", ");
    }
  }
  printf("\n");
  return neo_js_context_create_undefined(ctx);
}

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  neo_js_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(allocator, runtime);
  neo_js_variable_t println =
      neo_js_context_create_cfunction(ctx, L"println", js_println);
  neo_js_variable_t global = neo_js_context_get_global(ctx);
  neo_js_variable_t Object = neo_js_context_get_field(
      ctx, global, neo_js_context_create_string(ctx, L"Object"));
  neo_js_variable_t num = neo_js_context_create_number(ctx, 100);
  neo_js_variable_t boolean = neo_js_context_create_boolean(ctx, false);
  neo_js_variable_t args[] = {num, boolean};
  neo_js_context_call(ctx, println, neo_js_context_create_undefined(ctx),
                      sizeof(args) / sizeof(neo_js_variable_t), args);
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  neo_delete_allocator(allocator);
  return 0;
}