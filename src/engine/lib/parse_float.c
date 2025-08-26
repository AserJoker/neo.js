#include "engine/lib/parse_float.h"
#include "core/unicode.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
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
  const wchar_t *source = neo_js_variable_to_string(arg)->string;
  if (NEO_IS_SPACE(*source)) {
    source++;
  }
  wchar_t *next = NULL;
  double val = wcstod(source, &next);
  if (next == source) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, val);
}