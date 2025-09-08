#include "core/allocator.h"
#include "core/error.h"
#include "core/fs.h"
#include "engine/basetype/error.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#endif
NEO_JS_CFUNCTION(js_on_fulfilled) {
  neo_js_variable_t result = argv[0];
  result = neo_js_context_to_string(ctx, result);
  fprintf(stderr, "%s\n", neo_js_context_to_cstring(ctx, result));
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(js_on_rejected) {
  neo_js_variable_t result = argv[0];
  result = neo_js_context_to_string(ctx, result);
  fprintf(stderr, "Uncaught error: %s\n",
          neo_js_context_to_cstring(ctx, result));
  return neo_js_context_create_undefined(ctx);
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#else
  setlocale(LC_ALL, "");
#endif
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  neo_js_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(allocator, runtime);
  neo_js_context_init_std(ctx);
  char *buf = neo_fs_read_file(allocator, "../index.mjs");
  if (!buf) {
    fprintf(stderr, "cannot open file: ../index.mjs\n");
  } else {
    neo_js_variable_t result = neo_js_context_eval(ctx, "../index.mjs", buf);
    neo_allocator_free(allocator, buf);

    if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
      result = neo_js_error_get_error(ctx, result);
      result = neo_js_context_to_string(ctx, result);
      fprintf(stderr, "Uncaught %s\n", neo_js_context_to_cstring(ctx, result));
    } else {
      if (neo_js_context_is_thenable(ctx, result)) {
        neo_js_variable_t on_fullfilled =
            neo_js_context_create_cfunction(ctx, NULL, js_on_fulfilled);
        neo_js_variable_t on_rejected =
            neo_js_context_create_cfunction(ctx, NULL, js_on_rejected);
        neo_js_variable_t then = neo_js_context_get_field(
            ctx, result, neo_js_context_create_string(ctx, "then"), NULL);
        neo_js_variable_t args[] = {on_fullfilled, on_rejected};
        neo_js_context_call(ctx, then, result, 2, args);
      } else {
        result = neo_js_context_to_string(ctx, result);
        fprintf(stderr, "%s\n", neo_js_context_to_cstring(ctx, result));
      }
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