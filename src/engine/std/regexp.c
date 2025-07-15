#include "engine/std/regexp.h"
#include "core/allocator.h"
#include "core/regexp.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <wchar.h>

neo_js_variable_t neo_js_regexp_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  const wchar_t *rule = L"(?:)";
  const wchar_t *s_flag = L"";
  if (argc > 0) {
    neo_js_variable_t v_rule = neo_js_context_to_string(ctx, argv[0]);
    if (neo_js_variable_get_type(v_rule)->kind == NEO_TYPE_ERROR) {
      return v_rule;
    }
    rule = neo_js_variable_to_string(v_rule)->string;
  }
  if (argc > 1) {
    neo_js_variable_t v_flag = neo_js_context_to_string(ctx, argv[1]);
    if (neo_js_variable_get_type(v_flag)->kind == NEO_TYPE_ERROR) {
      return v_flag;
    }
    s_flag = neo_js_variable_to_string(v_flag)->string;
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint8_t *flag = neo_allocator_alloc(allocator, sizeof(uint8_t), NULL);
  *flag = 0;
  neo_js_context_set_opaque(ctx, self, L"[[flag]]", flag);
  size_t len = wcslen(rule) + wcslen(s_flag) + 8;
  wchar_t *str = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
  swprintf(str, len, L"/%ls/%ls", rule, s_flag);
  neo_js_context_set_opaque(ctx, self, L"[[string]]", str);
  const wchar_t *pflag = s_flag;
  while (*pflag != 0) {
    switch (*pflag) {
    case 'g':
      *flag |= NEO_REGEXP_FLAG_GLOBAL;
      break;
    case 'i':
      *flag |= NEO_REGEXP_FLAG_IGNORECASE;
      break;
    case 'm':
      *flag |= NEO_REGEXP_FLAG_MULTILINE;
      break;
    case 's':
      *flag |= NEO_REGEXP_FLAG_DOTALL;
      break;
    case 'u':
      *flag |= NEO_REGEXP_FLAG_UNICODE;
      break;
    case 'y':
      *flag |= NEO_REGEXP_FLAG_STICKY;
      break;
    default: {
      size_t len = wcslen(s_flag) + 128;
      wchar_t *message =
          neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
      swprintf(message, len,
               L"Invalid flags supplied to RegExp constructor '%ls'", s_flag);
      neo_js_variable_t error =
          neo_js_context_create_error(ctx, NEO_ERROR_SYNTAX, message);
      neo_allocator_free(allocator, message);
      return error;
    }
    }
    pflag++;
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_regexp_to_string(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  const wchar_t *str = neo_js_context_get_opaque(ctx, self, L"[[string]]");
  return neo_js_context_create_string(ctx, str);
}

neo_js_variable_t neo_js_regexp_exec(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_regexp_test(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}
