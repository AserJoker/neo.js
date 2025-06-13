
#include "core/allocator.h"
#include "core/error.h"
#include "js/context.h"
#include "js/runtime.h"
#include "js/string.h"
#include "js/type.h"
#include "js/variable.h"
#include <locale.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  char *old_locale = setlocale(LC_ALL, NULL);
  setlocale(LC_ALL, "");
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  neo_js_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(allocator, runtime);
  neo_js_variable_t string = neo_js_context_create_string(ctx, L"0b");
  neo_js_variable_t num = neo_js_context_to_number(ctx, string);
  neo_js_variable_t output = neo_js_context_to_string(ctx, num);
  const wchar_t *result =
      neo_js_value_to_string(neo_js_variable_get_value(output))->string;
  printf("%ls\n", result);
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  neo_delete_allocator(allocator);
  setlocale(LC_ALL, old_locale);
  return 0;
}