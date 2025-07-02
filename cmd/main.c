#include "core/allocator.h"
#include "core/error.h"
#include "engine/basetype/error.h"
#include "engine/basetype/number.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <locale.h>
#include <math.h>
#include <stdbool.h>
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

static neo_js_variable_t js_set_timeout(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  if (argc < 1) {
    return neo_js_context_create_error(
        ctx, NEO_ERROR_TYPE,
        L"The \" callback\" argument must be of type function.");
  }
  uint32_t timeout = 0;
  if (argc > 1) {
    neo_js_variable_t time = neo_js_context_to_number(ctx, argv[1]);
    neo_js_number_t num = neo_js_variable_to_number(time);
    if (isnan(num->number) || num->number <= 0) {
      timeout = 0;
    } else {
      timeout = num->number;
    }
  }
  uint32_t id =
      neo_js_context_create_macro_task(ctx, argv[0], 0, NULL, timeout, false);
  return neo_js_context_create_number(ctx, id);
}

static neo_js_variable_t js_clear_timeout(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  if (argc > 0) {
    neo_js_variable_t vid = neo_js_context_to_number(ctx, argv[0]);
    neo_js_number_t id = neo_js_variable_to_number(vid);
    if (!isnan(id->number) && id->number >= 0) {
      neo_js_context_kill_macro_task(ctx, id->number);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t js_set_interval(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  if (argc < 1) {
    return neo_js_context_create_error(
        ctx, NEO_ERROR_TYPE,
        L"The \" callback\" argument must be of type function.");
  }
  uint32_t timeout = 0;
  if (argc > 1) {
    neo_js_variable_t time = neo_js_context_to_number(ctx, argv[1]);
    neo_js_number_t num = neo_js_variable_to_number(time);
    if (isnan(num->number) || num->number <= 0) {
      timeout = 0;
    } else {
      timeout = num->number;
    }
  }
  uint32_t id =
      neo_js_context_create_macro_task(ctx, argv[0], 0, NULL, timeout, true);
  return neo_js_context_create_number(ctx, id);
}

static neo_js_variable_t js_clear_interval(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  if (argc > 0) {
    neo_js_variable_t vid = neo_js_context_to_number(ctx, argv[0]);
    neo_js_number_t id = neo_js_variable_to_number(vid);
    if (!isnan(id->number) && id->number >= 0) {
      neo_js_context_kill_macro_task(ctx, id->number);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

static void disp_js_variable(neo_js_context_t ctx, neo_js_variable_t variable) {
  if (neo_js_variable_get_type(variable)->kind == NEO_TYPE_ERROR) {
    neo_js_error_print(ctx, variable);
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
  neo_js_context_set_field(
      ctx, global, neo_js_context_create_string(ctx, L"setTimeout"),
      neo_js_context_create_cfunction(ctx, L"setTimeout", js_set_timeout));
  neo_js_context_set_field(
      ctx, global, neo_js_context_create_string(ctx, L"clearTimeout"),
      neo_js_context_create_cfunction(ctx, L"clearTimeout", js_clear_timeout));
  neo_js_context_set_field(
      ctx, global, neo_js_context_create_string(ctx, L"setInterval"),
      neo_js_context_create_cfunction(ctx, L"setInterval", js_set_interval));
  neo_js_context_set_field(ctx, global,
                           neo_js_context_create_string(ctx, L"clearInterval"),
                           neo_js_context_create_cfunction(
                               ctx, L"clearInterval", js_clear_interval));
  neo_js_variable_t result = neo_js_context_eval(ctx, "index.mjs", buf);
  disp_js_variable(ctx, result);
  while (!neo_js_context_is_ready(ctx)) {
    neo_js_context_next_tick(ctx);
  }
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  neo_allocator_free(allocator, buf);
  neo_delete_allocator(allocator);
  return 0;
}