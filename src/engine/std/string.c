#include "engine/std/string.h"
#include "core/allocator.h"
#include "core/string.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdint.h>
#include <wchar.h>

NEO_JS_CFUNCTION(neo_js_string_from_char_code) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  wchar_t *s =
      neo_allocator_alloc(allocator, (argc + 1) * sizeof(wchar_t), NULL);
  neo_js_context_defer_free(ctx, s);
  s[argc] = 0;
  for (size_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t variable = neo_js_context_to_integer(ctx, argv[idx]);
    s[idx] = (uint16_t)(neo_js_variable_to_number(variable)->number);
  }
  return neo_js_context_create_string(ctx, s);
}
NEO_JS_CFUNCTION(neo_js_string_from_code_point) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  size_t len = argc * 2 + 1;
  wchar_t *s = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
  neo_js_context_defer_free(ctx, s);
  wchar_t *dst = s;
  for (size_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t variable = neo_js_context_to_integer(ctx, argv[idx]);
    uint32_t utf32 = neo_js_variable_to_number(variable)->number;
    if (utf32 < 0xffff) {
      *dst++ = (uint16_t)utf32;
    } else if (utf32 < 0xeffff) {
      uint32_t offset = utf32 - 0x10000;
      *dst++ = (uint16_t)(0xd800 + (offset >> 10));
      *dst++ = (uint16_t)(0xdc00 + (offset & 0x3ff));
    }
  }
  *dst = 0;
  return neo_js_context_create_string(ctx, s);
}
NEO_JS_CFUNCTION(neo_js_string_raw) {
  if (!argc) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t qusis = argv[0];
  if (neo_js_variable_get_type(qusis)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        L"Cannot convert undefined or null to object");
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, qusis, neo_js_context_create_string(ctx, L"length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  int64_t length = neo_js_variable_to_number(vlength)->number;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  wchar_t *result = neo_allocator_alloc(allocator, sizeof(wchar_t) * 128, NULL);
  result[0] = 0;
  size_t max = 128;
  for (int64_t idx = 0; idx < length; idx++) {
    neo_js_variable_t vidx = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t str = neo_js_context_get_field(ctx, qusis, vidx, NULL);
    NEO_JS_TRY_AND_THROW(str);
    str = neo_js_context_to_string(ctx, str);
    NEO_JS_TRY_AND_THROW(str);
    result = neo_wstring_concat(allocator, result, &max, result);
    if (idx < argc) {
      neo_js_variable_t item = argv[0];
      item = neo_js_context_to_string(ctx, item);
      NEO_JS_TRY_AND_THROW(item);
      result = neo_wstring_concat(allocator, result, &max,
                                  neo_js_variable_to_string(item)->string);
    }
  }
  neo_js_context_defer_free(ctx, result);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_string_constructor) {
  neo_js_variable_t src = NULL;
  if (argc) {
    src = argv[0];
  } else {
    src = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_context_get_call_type(ctx) == NEO_JS_FUNCTION_CALL) {
    if (neo_js_variable_get_type(src)->kind == NEO_JS_TYPE_SYMBOL) {
      return neo_js_context_create_string(
          ctx, neo_js_variable_to_symbol(src)->string);
    } else {
      return neo_js_context_to_string(ctx, src);
    }
  }
  src = neo_js_context_to_string(ctx, src);
  NEO_JS_TRY_AND_THROW(src);
  neo_js_context_set_internal(ctx, self, L"[[primitive]]", src);
  neo_js_context_def_field(
      ctx, self, neo_js_context_create_string(ctx, L"length"),
      neo_js_context_create_number(
          ctx, wcslen(neo_js_variable_to_string(src)->string)),
      false, false, false);
  return self;
}
NEO_JS_CFUNCTION(neo_js_string_at);
NEO_JS_CFUNCTION(neo_js_string_char_at);
NEO_JS_CFUNCTION(neo_js_string_char_code_at);
NEO_JS_CFUNCTION(neo_js_string_char_point_at);
NEO_JS_CFUNCTION(neo_js_string_concat);
NEO_JS_CFUNCTION(neo_js_string_ends_with);
NEO_JS_CFUNCTION(neo_js_string_includes);
NEO_JS_CFUNCTION(neo_js_string_index_of);
NEO_JS_CFUNCTION(neo_js_string_is_well_formed);
NEO_JS_CFUNCTION(neo_js_string_last_index_of);
NEO_JS_CFUNCTION(neo_js_string_local_compare);
NEO_JS_CFUNCTION(neo_js_string_match);
NEO_JS_CFUNCTION(neo_js_string_match_all);
NEO_JS_CFUNCTION(neo_js_string_normalize);
NEO_JS_CFUNCTION(neo_js_string_pad_end);
NEO_JS_CFUNCTION(neo_js_string_pad_start);
NEO_JS_CFUNCTION(neo_js_string_repeat);
NEO_JS_CFUNCTION(neo_js_string_replace);
NEO_JS_CFUNCTION(neo_js_string_replace_all);
NEO_JS_CFUNCTION(neo_js_string_search);
NEO_JS_CFUNCTION(neo_js_string_slice);
NEO_JS_CFUNCTION(neo_js_string_split);
NEO_JS_CFUNCTION(neo_js_string_starts_with);
NEO_JS_CFUNCTION(neo_js_string_substring);
NEO_JS_CFUNCTION(neo_js_string_to_local_lower_case);
NEO_JS_CFUNCTION(neo_js_string_to_local_upper_case);
NEO_JS_CFUNCTION(neo_js_string_to_lower_case);
NEO_JS_CFUNCTION(neo_js_string_to_string);
NEO_JS_CFUNCTION(neo_js_string_to_upper_case);
NEO_JS_CFUNCTION(neo_js_string_to_well_formed);
NEO_JS_CFUNCTION(neo_js_string_trim);
NEO_JS_CFUNCTION(neo_js_string_trim_end);
NEO_JS_CFUNCTION(neo_js_string_trim_start);
NEO_JS_CFUNCTION(neo_js_string_value_of);
NEO_JS_CFUNCTION(neo_js_string_iterator);
void neo_js_context_init_std_string(neo_js_context_t ctx) {
  neo_js_variable_t constructor =
      neo_js_context_get_std(ctx).string_constructor;
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"prototype"), NULL);
  neo_js_variable_t global = neo_js_context_get_global(ctx);
  neo_js_context_set_field(ctx, global,
                           neo_js_context_create_string(ctx, L"String"),
                           constructor, NULL);
  NEO_JS_SET_METHOD(ctx, constructor, L"fromCharCode",
                    neo_js_string_from_char_code);
  NEO_JS_SET_METHOD(ctx, constructor, L"fromCodePoint",
                    neo_js_string_from_code_point);
  NEO_JS_SET_METHOD(ctx, constructor, L"raw", neo_js_string_raw);

  //   NEO_JS_SET_METHOD(ctx, prototype, L"at", neo_js_string_at);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"charAt", neo_js_string_char_at);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"charCodeAt",
  //   neo_js_string_char_code_at); NEO_JS_SET_METHOD(ctx, prototype,
  //   L"charPointAt",
  //                     neo_js_string_char_point_at);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"concat", neo_js_string_concat);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"endsWith", neo_js_string_ends_with);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"includes", neo_js_string_includes);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"indexOf", neo_js_string_index_of);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"isWellFormed",
  //                     neo_js_string_is_well_formed);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"lastIndexOf",
  //                     neo_js_string_last_index_of);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"localCompare",
  //                     neo_js_string_local_compare);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"match", neo_js_string_match);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"matchAll", neo_js_string_match_all);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"normalize", neo_js_string_normalize);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"padEnd", neo_js_string_pad_end);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"padStart", neo_js_string_pad_start);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"repeat", neo_js_string_repeat);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"replace", neo_js_string_replace);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"replaceAll",
  //   neo_js_string_replace_all); NEO_JS_SET_METHOD(ctx, prototype, L"search",
  //   neo_js_string_search); NEO_JS_SET_METHOD(ctx, prototype, L"slice",
  //   neo_js_string_slice); NEO_JS_SET_METHOD(ctx, prototype, L"split",
  //   neo_js_string_split); NEO_JS_SET_METHOD(ctx, prototype, L"startsWith",
  //   neo_js_string_starts_with); NEO_JS_SET_METHOD(ctx, prototype,
  //   L"substring", neo_js_string_substring); NEO_JS_SET_METHOD(ctx, prototype,
  //   L"toLocalLowerCase",
  //                     neo_js_string_to_local_lower_case);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"toLocalUpperCase",
  //                     neo_js_string_to_local_upper_case);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"toLowerCase",
  //                     neo_js_string_to_lower_case);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"toString", neo_js_string_to_string);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"toUpperCase",
  //                     neo_js_string_to_upper_case);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"toWellFormed",
  //                     neo_js_string_to_well_formed);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"trim", neo_js_string_trim);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"trimEnd", neo_js_string_trim_end);
  //   NEO_JS_SET_METHOD(ctx, prototype, L"trimStart",
  //   neo_js_string_trim_start); NEO_JS_SET_METHOD(ctx, prototype, L"valueOf",
  //   neo_js_string_value_of);
}