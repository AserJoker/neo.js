#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "engine/basetype/error.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/stackframe.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

static neo_js_variable_t js_println(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t string = neo_js_context_to_string(ctx, argv[idx]);
    neo_js_string_t str = neo_js_variable_to_string(string);
    printf("%ls", str->string);
    if (idx != argc - 1) {
      printf(", ");
    }
  }
  printf("\n");
  return neo_js_context_create_undefined(ctx);
}
static void disp_js_variable(neo_js_context_t ctx, neo_js_variable_t variable) {
  if (neo_js_variable_get_type(variable)->kind == NEO_TYPE_ERROR) {
    neo_js_error_type_t type = neo_js_error_get_type(variable);
    if (type == NEO_ERROR_CUSTOM) {
      neo_js_variable_t custom = neo_js_error_get_custom(ctx, variable);
      custom = neo_js_context_to_string(ctx, custom);
      neo_js_string_t string = neo_js_variable_to_string(custom);
      fprintf(stderr, "Uncaught Error: %ls\n", string->string);
    } else {
      const wchar_t *message = neo_js_error_get_message(variable);
      neo_list_t stacktrace = neo_js_error_get_stacktrace(variable);
      const wchar_t *stype = NULL;
      switch (type) {
      case NEO_ERROR_SYNTAX:
        stype = L"SyntaxError";
        break;
      case NEO_ERROR_RANGE:
        stype = L"RangeError";
        break;
      case NEO_ERROR_TYPE:
        stype = L"TypeError";
        break;
      case NEO_ERROR_REFERENCE:
        stype = L"ReferenceError";
        break;
      default:
        break;
      }
      printf("Uncaught %ls: %ls\n", stype, message);
      for (neo_list_node_t it = neo_list_get_first(stacktrace);
           it != neo_list_get_tail(stacktrace); it = neo_list_node_next(it)) {
        neo_js_stackframe_t frame = neo_list_node_get(it);
        if (frame->filename) {
          printf("  at %ls(%ls:%d:%d)\n", frame->function, frame->filename,
                 frame->line, frame->column);
        } else {
          printf("  at %ls(<internal>)\n", frame->function);
        }
      }
    }
  } else {
    neo_js_variable_t string = neo_js_context_to_string(ctx, variable);
    neo_js_string_t str = neo_js_variable_to_string(string);
    printf("%ls\n", str->string);
  }
}

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  neo_allocator_t allocator = neo_create_default_allocator();
  FILE *fp = fopen("../index.mjs", "rb");
  if (!fp) {
    neo_delete_allocator(allocator);
    fprintf(stderr, "Failed open index.mjs\n");
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  size_t len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *buf = neo_allocator_alloc(allocator, len + 1, NULL);
  buf[len] = 0;
  fread(buf, len, 1, fp);
  fclose(fp);
  neo_error_initialize(allocator);
  neo_js_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(allocator, runtime);
  neo_js_variable_t println =
      neo_js_context_create_cfunction(ctx, L"println", js_println);
  neo_js_variable_t global = neo_js_context_get_global(ctx);
  neo_js_context_set_field(
      ctx, global, neo_js_context_create_string(ctx, L"println"), println);
  neo_js_variable_t result = neo_js_context_eval(ctx, "index.mjs", buf);
  disp_js_variable(ctx, result);
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  // neo_ast_node_t node = TRY(neo_ast_parse_code(allocator, "index.mjs", buf))
  // {
  //   neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
  //   char *s = neo_error_to_string(error);
  //   printf("%s\n", s);
  //   neo_allocator_free(allocator, s);
  //   neo_allocator_free(allocator, error);
  //   neo_allocator_free(allocator, buf);
  //   neo_delete_allocator(allocator);
  //   return -1;
  // }
  // neo_variable_t val = neo_ast_node_serialize(allocator, node);
  // char *json = neo_json_stringify(allocator, val);
  // printf("%s\n", json);
  // neo_allocator_free(allocator, json);
  // neo_allocator_free(allocator, val);
  // neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, buf);
  neo_delete_allocator(allocator);
  return 0;
}