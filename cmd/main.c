#include "core/allocator.h"
#include "core/error.h"
#include "core/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/stackframe.h"
#include "engine/string.h"
#include "engine/symbol.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <locale.h>
#endif

NEO_JS_CFUNCTION(print) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t value = argv[idx];
    if (idx != 0) {
      printf(", ");
    }
    if (value->value->type == NEO_JS_TYPE_SYMBOL) {
      neo_js_symbol_t symbol = (neo_js_symbol_t)value->value;
      char *description =
          neo_string16_to_string(allocator, symbol->description);
      printf("Symbol(%s)", description);
      neo_allocator_free(allocator, description);
    } else {
      value = neo_js_variable_to_string(value, ctx);
      char *str = neo_string16_to_string(
          allocator, ((neo_js_string_t)value->value)->value);
      printf("%s", str);
      neo_allocator_free(allocator, str);
    }
  }
  printf("\n");
  return neo_js_context_get_undefined(ctx);
}
static void neo_js_handle_dispose(neo_allocator_t allocator,
                                  neo_js_handle_t self) {
  neo_deinit_js_handle(self, allocator);
}
static neo_js_handle_t neo_create_js_handle(neo_allocator_t allocator,
                                            neo_js_handle_type_t type) {
  neo_js_handle_t handle = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_handle_t), neo_js_handle_dispose);
  neo_init_js_handle(handle, allocator, type);
  return handle;
}
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
  neo_js_variable_t js_print =
      neo_js_context_create_cfunction(ctx, print, "print");
  neo_js_variable_t global = neo_js_context_get_global(ctx);
  neo_js_variable_set_field(
      global, ctx, neo_js_context_create_cstring(ctx, "print"), js_print);
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