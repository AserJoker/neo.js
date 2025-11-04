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

  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);

  neo_js_variable_t clazz = constant->object_class;
  neo_js_variable_t prototype = constant->object_prototype;
  neo_js_variable_t constructor =
      neo_js_variable_get_field(prototype, ctx, constant->key_constructor);
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, rt);
  neo_delete_allocator(allocator);
  return 0;
}