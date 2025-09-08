#include "engine/lib/parse_int.h"
#include "core/unicode.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdlib.h>
#include <wchar.h>
NEO_JS_CFUNCTION(neo_js_parse_int) {
  if (argc < 1) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = argv[0];
  if (neo_js_variable_get_type(arg)->kind == NEO_JS_TYPE_NUMBER) {
    return arg;
  }
  if (neo_js_variable_get_type(arg)->kind != NEO_JS_TYPE_STRING) {
    arg = neo_js_context_to_string(ctx, arg);
    NEO_JS_TRY_AND_THROW(arg);
  }
  int radix = 0;
  if (argc > 1) {
    neo_js_variable_t vradix = argv[1];
    if (neo_js_variable_get_type(vradix)->kind == NEO_JS_TYPE_NUMBER) {
      double v = neo_js_variable_to_number(vradix)->number;

      radix = (int)v;
    } else {
      if (neo_js_variable_get_type(vradix)->kind != NEO_JS_TYPE_STRING) {
        vradix = neo_js_context_to_string(ctx, vradix);
        NEO_JS_TRY_AND_THROW(vradix);
      }
      const char *sradix = neo_js_context_to_cstring(ctx, vradix);
      neo_utf8_char utf8 = neo_utf8_read_char(sradix);
      uint32_t utf32 = neo_utf8_char_to_utf32(utf8);
      while (NEO_IS_SPACE(utf32)) {
        sradix = utf8.end;
        utf8 = neo_utf8_read_char(sradix);
        utf32 = neo_utf8_char_to_utf32(utf8);
      }
      char *next = NULL;
      if (sradix[0] == '0') {
        if (sradix[1] == 'x' || sradix[1] == 'X') {
          radix = strtol(sradix + 2, &next, 16);
        } else {
          radix = strtol(sradix, &next, 8);
        }
      } else if (sradix[0] >= '0' && sradix[0] <= '9') {
        radix = strtol(sradix, &next, 10);
      }
    }
    if (radix < 2 || radix > 36) {
      return neo_js_context_create_number(ctx, NAN);
    }
  }
  const char *source = neo_js_context_to_cstring(ctx, arg);
  neo_utf8_char utf8 = neo_utf8_read_char(source);
  uint32_t utf32 = neo_utf8_char_to_utf32(utf8);
  if (NEO_IS_SPACE(utf32)) {
    source = utf8.end;
    utf8 = neo_utf8_read_char(source);
    utf32 = neo_utf8_char_to_utf32(utf8);
  }
  char *next = NULL;
  int64_t val = strtoll(source, &next, radix);
  if (next == source) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, val);
}