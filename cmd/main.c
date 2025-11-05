#include "core/allocator.h"
#include "core/error.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <locale.h>
#endif
NEO_JS_CFUNCTION(fn1) { return neo_js_context_create_number(ctx, 100); }
NEO_JS_CFUNCTION(fn2) { return self; }

int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#else
  setlocale(LC_ALL, "");
#endif
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  neo_js_runtime_t rt = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(rt);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t neo_js_fn1 =
      neo_js_context_create_cfunction(ctx, fn1, "f1");
  neo_js_variable_t val = neo_js_variable_call(
      neo_js_fn1, ctx, neo_js_context_create_undefined(ctx), 0, NULL);
  neo_js_context_pop_scope(ctx);
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, rt);
  neo_delete_allocator(allocator);
  return 0;
}