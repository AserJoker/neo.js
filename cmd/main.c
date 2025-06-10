
#include "core/allocator.h"
#include "core/error.h"
#include "core/unicode.h"
#include "js/context.h"
#include "js/runtime.h"
#include "js/string.h"
#include "js/type.h"
#include "js/variable.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
  neo_utf8_char ustr = neo_utf8_read_char("中");
  uint32_t utf32 = neo_utf8_char_to_utf32(ustr);
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  neo_js_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(allocator, runtime);
  neo_js_variable_t num = neo_js_context_create_number(ctx, 123.0f);
  neo_js_variable_t str = neo_js_context_tostring(ctx, num);
  neo_js_string_t string =
      neo_js_value_to_string(neo_js_variable_get_value(str));
  printf("%ls\n", neo_js_string_get_value(string));
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  neo_delete_allocator(allocator);
  return 0;
}