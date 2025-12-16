#include "core/allocator.h"
#include "core/error.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/stackframe.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <locale.h>
#endif
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
  neo_js_variable_t res = neo_js_context_run(ctx, "../index.mjs");
  if (res->value->type != NEO_JS_TYPE_EXCEPTION) {
    while (neo_js_context_has_task(ctx)) {
      neo_js_context_next_task(ctx);
    }
  } else {
    neo_js_error_callback cb = neo_js_context_get_error_callback(ctx);
    cb(ctx, res);
  }
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, rt);
  neo_delete_allocator(allocator);
  return 0;
}