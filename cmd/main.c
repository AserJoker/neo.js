#include "core/allocator.h"
#include "core/error.h"
#include "core/fs.h"
#include "core/list.h"
#include "core/string.h"
#include "engine/context.h"
#include "engine/exception.h"
#include "engine/runtime.h"
#include "engine/stackframe.h"
#include "engine/string.h"
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

int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#else
  setlocale(LC_ALL, "");
#endif
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  char *source = neo_fs_read_file(allocator, "../index.mjs");
  neo_js_runtime_t rt = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(rt);
  neo_js_variable_t res = neo_js_context_eval(ctx, source, "index.mjs");
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)res->value)->error);
    neo_js_variable_t string = neo_js_variable_to_string(error, ctx);
    char *utf8 = NULL;
    if (string->value->type != NEO_JS_TYPE_STRING) {
      utf8 = neo_create_string(allocator, "unknown exception");
    } else {
      const uint16_t *str = NULL;
      str = ((neo_js_string_t)string->value)->value;
      utf8 = neo_string16_to_string(allocator, str);
    };
    printf("%s\n", utf8);
    neo_allocator_free(allocator, utf8);
    neo_list_t trace = ((neo_js_exception_t)res->value)->trace;
    neo_list_node_t it = neo_list_get_last(trace);
    while (it != neo_list_get_head(trace)) {
      neo_js_stackframe_t frame = neo_list_node_get(it);
      uint16_t *string = neo_js_stackframe_to_string(allocator, frame);
      char *utf8 = neo_string16_to_string(allocator, string);
      printf("    at %s\n", utf8);
      neo_allocator_free(allocator, utf8);
      neo_allocator_free(allocator, string);
      it = neo_list_node_last(it);
    }
  } else {
    neo_js_variable_t string = neo_js_variable_to_string(res, ctx);
    const uint16_t *str = ((neo_js_string_t)string->value)->value;
    char *utf8 = neo_string16_to_string(allocator, str);
    printf("%s\n", utf8);
    neo_allocator_free(allocator, utf8);
  }
  neo_allocator_free(allocator, source);
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, rt);
  neo_delete_allocator(allocator);
  return 0;
}