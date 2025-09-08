#include "engine/lib/parse_float.h"
#include "core/unicode.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdlib.h>
NEO_JS_CFUNCTION(neo_js_parse_float) {
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
  const char *source = neo_js_context_to_cstring(ctx, arg);
  neo_utf8_char utf8 = neo_utf8_read_char(source);
  uint32_t utf32 = neo_utf8_char_to_utf32(utf8);
  if (NEO_IS_SPACE(utf32)) {
    source = utf8.end;
    utf8 = neo_utf8_read_char(source);
    utf32 = neo_utf8_char_to_utf32(utf8);
  }
  char *next = NULL;
  double val = strtof(source, &next);
  if (next == source) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, val);
}