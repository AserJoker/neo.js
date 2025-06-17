
#include "core/allocator.h"
#include "core/error.h"
#include "js/context.h"
#include "js/runtime.h"
#include "js/type.h"
#include <locale.h>

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  neo_js_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(allocator, runtime);
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  neo_delete_allocator(allocator);
  return 0;
}