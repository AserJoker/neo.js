#include "engine/std/number.h"
#include "core/bigint.h"
#include "engine/basetype/number.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdint.h>
#include <wchar.h>
NEO_JS_CFUNCTION(neo_js_number_is_finite) {
  if (!argc) {
    return neo_js_context_create_boolean(ctx, false);
  }
  if (neo_js_variable_get_type(argv[0])->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_boolean(ctx, false);
  }
  double val = neo_js_variable_to_number(argv[0])->number;
  if (isnan(val) || isinf(val)) {
    return neo_js_context_create_boolean(ctx, false);
  }
  return neo_js_context_create_boolean(ctx, true);
}
NEO_JS_CFUNCTION(neo_js_number_is_integer) {
  if (!argc) {
    return neo_js_context_create_boolean(ctx, false);
  }
  if (neo_js_variable_get_type(argv[0])->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_boolean(ctx, false);
  }
  double val = neo_js_variable_to_number(argv[0])->number;
  int64_t i = (uint64_t)val;
  return neo_js_context_create_boolean(ctx, val == i);
}
NEO_JS_CFUNCTION(neo_js_number_is_nan) {
  if (!argc) {
    return neo_js_context_create_boolean(ctx, false);
  }
  if (neo_js_variable_get_type(argv[0])->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_boolean(ctx, false);
  }
  double val = neo_js_variable_to_number(argv[0])->number;
  if (isnan(val)) {
    return neo_js_context_create_boolean(ctx, true);
  }
  return neo_js_context_create_boolean(ctx, false);
}
NEO_JS_CFUNCTION(neo_js_number_is_safe_integer) {
  if (!argc) {
    return neo_js_context_create_boolean(ctx, false);
  }
  if (neo_js_variable_get_type(argv[0])->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_boolean(ctx, false);
  }
  double val = neo_js_variable_to_number(argv[0])->number;
  if (val < -NEO_MAX_INTEGER || val > NEO_MAX_INTEGER) {
    return neo_js_context_create_boolean(ctx, false);
  }
  return neo_js_context_create_boolean(ctx, true);
}
NEO_JS_CFUNCTION(neo_js_number_constructor) {
  if (neo_js_context_get_call_type(ctx) == NEO_JS_FUNCTION_CALL) {
    if (!argc) {
      return neo_js_context_create_number(ctx, 0);
    }
    if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_BIGINT) {
      neo_bigint_t bigint = neo_js_variable_to_bigint(argv[0])->bigint;
      return neo_js_context_create_number(ctx, neo_bigint_to_number(bigint));
    }
    return neo_js_context_to_number(ctx, argv[0]);
  } else {
    neo_js_variable_t num = NULL;
    if (argc) {
      num = neo_js_context_to_number(ctx, argv[0]);
    } else {
      num = neo_js_context_create_number(ctx, NAN);
    }
    neo_js_context_set_internal(ctx, self, L"[[primitive]]", num);
    return neo_js_context_create_undefined(ctx);
  }
}

NEO_JS_CFUNCTION(neo_js_number_to_exponential) {
  neo_js_variable_t primitive =
      neo_js_context_get_internal(ctx, self, L"[[primitive]]");
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Number.prototype.toExponential requires that 'this' be a Number");
  }
  double val = neo_js_variable_to_number(primitive)->number;
  if (argc) {
    neo_js_variable_t f = neo_js_context_to_integer(ctx, argv[0]);
    int64_t r = neo_js_variable_to_number(f)->number;
    if (r < 0 || r > 100) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_RANGE,
          L"toExponential() argument must be between 0 and 100");
    }
    wchar_t format[16];
    swprintf(format, 16, L"%%.%de", r);
    wchar_t msg[256];
    swprintf(msg, 256, format, val);
    return neo_js_context_create_string(ctx, msg);
  }
  wchar_t msg[256];
  swprintf(msg, 256, L"%e", val);
  return neo_js_context_create_string(ctx, msg);
}
NEO_JS_CFUNCTION(neo_js_number_to_fixed) {
  neo_js_variable_t primitive =
      neo_js_context_get_internal(ctx, self, L"[[primitive]]");
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Number.prototype.toFixed requires that 'this' be a Number");
  }
  double val = neo_js_variable_to_number(primitive)->number;
  if (argc) {
    neo_js_variable_t f = neo_js_context_to_integer(ctx, argv[0]);
    int64_t r = neo_js_variable_to_number(f)->number;
    if (r < 0 || r > 100) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_RANGE,
          L"toFixed() argument must be between 0 and 100");
    }
    wchar_t format[16];
    swprintf(format, 16, L"%%.%dlf", r);
    wchar_t msg[256];
    swprintf(msg, 256, format, val);
    return neo_js_context_create_string(ctx, msg);
  }
  wchar_t msg[256];
  swprintf(msg, 256, L"%lg", val);
  return neo_js_context_create_string(ctx, msg);
}
NEO_JS_CFUNCTION(neo_js_number_to_local_string) {
  return neo_js_number_to_string(ctx, self, argc, argv);
}
NEO_JS_CFUNCTION(neo_js_number_to_precision) {
  if (!argc) {
    return neo_js_number_to_string(ctx, self, argc, argv);
  }
  neo_js_variable_t primitive =
      neo_js_context_get_internal(ctx, self, L"[[primitive]]");
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Number.prototype.toPrecision requires that 'this' be a Number");
  }
  double val = neo_js_variable_to_number(primitive)->number;
  if (argc) {
    neo_js_variable_t f = neo_js_context_to_integer(ctx, argv[0]);
    int64_t r = neo_js_variable_to_number(f)->number;
    if (r < 0 || r > 100) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_RANGE,
          L"toPrecision() argument must be between 0 and 100");
    }
    wchar_t format[16];
    swprintf(format, 16, L"%%.%dlg", r);
    wchar_t msg[256];
    swprintf(msg, 256, format, val);
    return neo_js_context_create_string(ctx, msg);
  }
  wchar_t msg[256];
  swprintf(msg, 256, L"%lg", val);
  return neo_js_context_create_string(ctx, msg);
}
NEO_JS_CFUNCTION(neo_js_number_to_string) {
  neo_js_variable_t primitive =
      neo_js_context_get_internal(ctx, self, L"[[primitive]]");
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Number.prototype.toString requires that 'this' be a Number");
  }
  if (!argc) {
    return neo_js_context_to_string(ctx, primitive);
  }
  double val = neo_js_variable_to_number(primitive)->number;
  neo_js_variable_t radix = neo_js_context_to_integer(ctx, argv[0]);
  int64_t base = neo_js_variable_to_number(radix)->number;
  if (base < 2 || base > 36) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_RANGE,
        L"toString() radix argument must be between 2 and 36");
  }
  int64_t precision = 16;
  wchar_t msg[1024];
  size_t idx = 0;
  if (val < 0) {
    val = -val;
    msg[0] = '-';
    idx++;
  }
  int64_t integer = (int64_t)val;
  double fractional = val - integer;
  char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  if (integer == 0) {
    msg[idx++] = '0';
  } else {
    wchar_t tmp[512];
    size_t i = 0;
    while (integer > 0) {
      tmp[i++] = digits[integer % base];
      integer /= base;
    }
    for (size_t j = 0; j < i; j++) {
      msg[idx++] = tmp[i - 1 - j];
    }
  }
  if (integer != val) {
    msg[idx++] = '.';
    wchar_t tmp[512];
    size_t i = 0;
    while (fractional > 0 && i < precision) {
      fractional *= base;
      tmp[idx++] = digits[(int)fractional];
      fractional -= (int)fractional;
      i++;
    }
  }
  msg[idx] = 0;
  return neo_js_context_create_string(ctx, msg);
}
NEO_JS_CFUNCTION(neo_js_number_value_of) {
  neo_js_variable_t primitive =
      neo_js_context_get_internal(ctx, self, L"[[primitive]]");
  if (!primitive ||
      neo_js_variable_get_type(primitive)->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Number.prototype.valueOf requires that 'this' be a Number");
  }
  return primitive;
}