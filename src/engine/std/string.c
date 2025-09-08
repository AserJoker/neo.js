#include "engine/std/string.h"
#include "core/allocator.h"
#include "core/string.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdint.h>
#include <string.h>
#include <wchar.h>

NEO_JS_CFUNCTION(neo_js_string_from_char_code) {
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_string_from_code_point) {
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_string_raw) {
  if (!argc) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Cannot convert undefined or null to object");
  }
  neo_js_variable_t qusis = argv[0];
  if (neo_js_variable_get_type(qusis)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Cannot convert undefined or null to object");
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, qusis, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  int64_t length = neo_js_variable_to_number(vlength)->number;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *result = neo_allocator_alloc(allocator, sizeof(char) * 128, NULL);
  result[0] = 0;
  size_t max = 128;
  for (int64_t idx = 0; idx < length; idx++) {
    neo_js_variable_t vidx = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t str = neo_js_context_get_field(ctx, qusis, vidx, NULL);
    NEO_JS_TRY_AND_THROW(str);
    str = neo_js_context_to_string(ctx, str);
    NEO_JS_TRY_AND_THROW(str);
    result = neo_string_concat(allocator, result, &max, result);
    if (idx < argc) {
      neo_js_variable_t item = argv[0];
      item = neo_js_context_to_string(ctx, item);
      NEO_JS_TRY_AND_THROW(item);
      result = neo_string_concat(allocator, result, &max,
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
  neo_js_context_set_internal(ctx, self, "[[primitive]]", src);
  neo_js_context_def_field(
      ctx, self, neo_js_context_create_string(ctx, "length"),
      neo_js_context_create_number(
          ctx, strlen(neo_js_variable_to_string(src)->string)),
      false, false, false);
  return self;
}
NEO_JS_CFUNCTION(neo_js_string_at) {
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_string_char_at) {
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_string_char_code_at) {
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_string_code_point_at) {
  return neo_js_context_create_undefined(ctx);
}
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
      ctx, constructor, neo_js_context_create_string(ctx, "prototype"), NULL);
  neo_js_variable_t global = neo_js_context_get_global(ctx);
  neo_js_context_set_field(ctx, global,
                           neo_js_context_create_string(ctx, "String"),
                           constructor, NULL);
  NEO_JS_SET_METHOD(ctx, constructor, "fromCharCode",
                    neo_js_string_from_char_code);
  NEO_JS_SET_METHOD(ctx, constructor, "fromCodePoint",
                    neo_js_string_from_code_point);
  NEO_JS_SET_METHOD(ctx, constructor, "raw", neo_js_string_raw);

  NEO_JS_SET_METHOD(ctx, prototype, "at", neo_js_string_at);
  NEO_JS_SET_METHOD(ctx, prototype, "charAt", neo_js_string_char_at);
  NEO_JS_SET_METHOD(ctx, prototype, "charCodeAt", neo_js_string_char_code_at);
  NEO_JS_SET_METHOD(ctx, prototype, "codePointAt", neo_js_string_code_point_at);
  //   NEO_JS_SET_METHOD(ctx, prototype, "concat", neo_js_string_concat);
  //   NEO_JS_SET_METHOD(ctx, prototype, "endsWith", neo_js_string_ends_with);
  //   NEO_JS_SET_METHOD(ctx, prototype, "includes", neo_js_string_includes);
  //   NEO_JS_SET_METHOD(ctx, prototype, "indexOf", neo_js_string_index_of);
  //   NEO_JS_SET_METHOD(ctx, prototype, "isWellFormed",
  //                     neo_js_string_is_well_formed);
  //   NEO_JS_SET_METHOD(ctx, prototype, "lastIndexOf",
  //                     neo_js_string_last_index_of);
  //   NEO_JS_SET_METHOD(ctx, prototype, "localCompare",
  //                     neo_js_string_local_compare);
  //   NEO_JS_SET_METHOD(ctx, prototype, "match", neo_js_string_match);
  //   NEO_JS_SET_METHOD(ctx, prototype, "matchAll", neo_js_string_match_all);
  //   NEO_JS_SET_METHOD(ctx, prototype, "normalize", neo_js_string_normalize);
  //   NEO_JS_SET_METHOD(ctx, prototype, "padEnd", neo_js_string_pad_end);
  //   NEO_JS_SET_METHOD(ctx, prototype, "padStart", neo_js_string_pad_start);
  //   NEO_JS_SET_METHOD(ctx, prototype, "repeat", neo_js_string_repeat);
  //   NEO_JS_SET_METHOD(ctx, prototype, "replace", neo_js_string_replace);
  //   NEO_JS_SET_METHOD(ctx, prototype, "replaceAll",
  //   neo_js_string_replace_all); NEO_JS_SET_METHOD(ctx, prototype, "search",
  //   neo_js_string_search); NEO_JS_SET_METHOD(ctx, prototype, "slice",
  //   neo_js_string_slice); NEO_JS_SET_METHOD(ctx, prototype, "split",
  //   neo_js_string_split); NEO_JS_SET_METHOD(ctx, prototype, "startsWith",
  //   neo_js_string_starts_with); NEO_JS_SET_METHOD(ctx, prototype,
  //   "substring", neo_js_string_substring); NEO_JS_SET_METHOD(ctx, prototype,
  //   "toLocalLowerCase",
  //                     neo_js_string_to_local_lower_case);
  //   NEO_JS_SET_METHOD(ctx, prototype, "toLocalUpperCase",
  //                     neo_js_string_to_local_upper_case);
  //   NEO_JS_SET_METHOD(ctx, prototype, "toLowerCase",
  //                     neo_js_string_to_lower_case);
  //   NEO_JS_SET_METHOD(ctx, prototype, "toString", neo_js_string_to_string);
  //   NEO_JS_SET_METHOD(ctx, prototype, "toUpperCase",
  //                     neo_js_string_to_upper_case);
  //   NEO_JS_SET_METHOD(ctx, prototype, "toWellFormed",
  //                     neo_js_string_to_well_formed);
  //   NEO_JS_SET_METHOD(ctx, prototype, "trim", neo_js_string_trim);
  //   NEO_JS_SET_METHOD(ctx, prototype, "trimEnd", neo_js_string_trim_end);
  //   NEO_JS_SET_METHOD(ctx, prototype, "trimStart",
  //   neo_js_string_trim_start); NEO_JS_SET_METHOD(ctx, prototype, "valueOf",
  //   neo_js_string_value_of);
}