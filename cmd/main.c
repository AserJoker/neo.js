#include "core/allocator.h"
#include "core/error.h"
#include "engine/scope.h"
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
  neo_js_scope_t scope = neo_create_js_scope(allocator, NULL);
  neo_js_variable_t a = neo_js_scope_create_variable(scope, NULL, NULL);
  neo_js_scope_t sub = neo_create_js_scope(allocator, scope);
  neo_js_variable_t b = neo_js_scope_create_variable(sub, NULL, NULL);
  neo_js_variable_t v = neo_js_scope_create_variable(sub, NULL, NULL);
  neo_js_variable_add_parent(v, b);
  neo_js_variable_add_parent(b, a);
  neo_allocator_free(allocator, sub);
  neo_allocator_free(allocator, scope);
  neo_delete_allocator(allocator);
  return 0;
}