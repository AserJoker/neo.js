#include "core/allocator.h"
#include "core/error.h"
#include "core/fs.h"
#include "engine/basetype/error.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <locale.h>
#include <stdbool.h>
#include <stddef.h>

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  neo_js_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(allocator, runtime);
  char *buf = neo_fs_read_file(allocator, L"../index.mjs");
  if (!buf) {
    fprintf(stderr, "cannot open file: ../index.mjs\n");
  } else {
    neo_js_variable_t result = neo_js_context_eval(ctx, L"../index.mjs", buf);
    neo_allocator_free(allocator, buf);

    if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
      result = neo_js_error_get_error(ctx, result);
      result = neo_js_context_to_string(ctx, result);
      fprintf(stderr, "Uncaught %ls\n",
              neo_js_variable_to_string(result)->string);
    } else {
      result = neo_js_context_to_string(ctx, result);
      fprintf(stderr, "%ls\n",
              neo_js_variable_to_string(result)->string);
      while (!neo_js_context_is_ready(ctx)) {
        neo_js_context_next_tick(ctx);
      }
    }
  }
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  neo_delete_allocator(allocator);
  return 0;
}