
#include "core/allocator.h"
#include "core/error.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <locale.h>
#include <stdio.h>

static neo_engine_variable_t js_println(neo_engine_context_t ctx,
                                        neo_engine_variable_t self,
                                        uint32_t argc,
                                        neo_engine_variable_t *argv) {
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_engine_variable_t string = neo_engine_context_to_string(ctx, argv[idx]);
    neo_engine_string_t str = neo_engine_variable_to_string(string);
    printf("%ls", str->string);
    if (idx != argc - 1) {
      printf(", ");
    }
  }
  printf("\n");
  return neo_engine_context_create_undefined(ctx);
}

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  neo_engine_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_engine_context_t ctx = neo_create_js_context(allocator, runtime);
  neo_engine_variable_t arr = neo_engine_context_create_array(ctx);
  neo_engine_variable_t num = neo_engine_context_create_number(ctx, 123);
  neo_engine_variable_t ss =
      neo_engine_context_create_string(ctx, L"hello world");
  neo_engine_context_set_field(ctx, arr,
                               neo_engine_context_create_number(ctx, 0), num);
  neo_engine_context_set_field(ctx, arr,
                               neo_engine_context_create_number(ctx, 1), ss);
  neo_engine_variable_t str = neo_engine_context_to_string(ctx, arr);
  neo_engine_string_t string = neo_engine_variable_to_string(str);
  printf("%ls\n", string->string);
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  neo_delete_allocator(allocator);
  return 0;
}