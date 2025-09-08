#include "engine/std/math.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdlib.h>
NEO_JS_CFUNCTION(neo_js_math_abs) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, val);
}
NEO_JS_CFUNCTION(neo_js_math_acos) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, acos(val));
}
NEO_JS_CFUNCTION(neo_js_math_acosh) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, acosh(val));
}
NEO_JS_CFUNCTION(neo_js_math_asin) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, asin(val));
}
NEO_JS_CFUNCTION(neo_js_math_asinh) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, asinh(val));
}
NEO_JS_CFUNCTION(neo_js_math_atan) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, atan(val));
}
NEO_JS_CFUNCTION(neo_js_math_atan2) {
  if (argc < 2) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double valx = neo_js_variable_to_number(x)->number;
  neo_js_variable_t y = neo_js_context_to_number(ctx, argv[1]);
  NEO_JS_TRY_AND_THROW(y);
  double valy = neo_js_variable_to_number(y)->number;
  return neo_js_context_create_number(ctx, atan2(valx, valy));
}
NEO_JS_CFUNCTION(neo_js_math_atanh) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, atanh(val));
}
NEO_JS_CFUNCTION(neo_js_math_cbrt) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, cbrt(val));
}
NEO_JS_CFUNCTION(neo_js_math_ceil) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, ceil(val));
}
NEO_JS_CFUNCTION(neo_js_math_clz32) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_integer(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  uint32_t val = neo_js_variable_to_number(x)->number;
  int64_t idx = 0;
  while (idx < 32) {
    if (val >> idx == 1) {
      break;
    }
    idx += 1;
  }
  return neo_js_context_create_number(ctx, idx);
}
NEO_JS_CFUNCTION(neo_js_math_cos) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, cos(val));
}
NEO_JS_CFUNCTION(neo_js_math_cosh) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, cosh(val));
}
NEO_JS_CFUNCTION(neo_js_math_exp) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, exp(val));
}
NEO_JS_CFUNCTION(neo_js_math_expm1) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, expm1(val));
}
NEO_JS_CFUNCTION(neo_js_math_floor) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, floor(val));
}
NEO_JS_CFUNCTION(neo_js_math_fround) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, (float)(val));
}
NEO_JS_CFUNCTION(neo_js_math_hypot) {
  if (argc < 2) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double valx = neo_js_variable_to_number(x)->number;
  neo_js_variable_t y = neo_js_context_to_number(ctx, argv[1]);
  NEO_JS_TRY_AND_THROW(y);
  double valy = neo_js_variable_to_number(y)->number;
  return neo_js_context_create_number(ctx, hypot(valx, valy));
}
NEO_JS_CFUNCTION(neo_js_math_imul) {
  if (argc < 2) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  int32_t valx = neo_js_variable_to_number(x)->number;
  neo_js_variable_t y = neo_js_context_to_number(ctx, argv[1]);
  NEO_JS_TRY_AND_THROW(y);
  int32_t valy = neo_js_variable_to_number(y)->number;
  return neo_js_context_create_number(ctx, valx * valy);
}
NEO_JS_CFUNCTION(neo_js_math_log) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, log(val));
}
NEO_JS_CFUNCTION(neo_js_math_log1p) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, log1p(val));
}
NEO_JS_CFUNCTION(neo_js_math_log2) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, log2(val));
}
NEO_JS_CFUNCTION(neo_js_math_log10) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, log10(val));
}
NEO_JS_CFUNCTION(neo_js_math_max) {
  if (argc < 2) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double valx = neo_js_variable_to_number(x)->number;
  neo_js_variable_t y = neo_js_context_to_number(ctx, argv[1]);
  NEO_JS_TRY_AND_THROW(y);
  double valy = neo_js_variable_to_number(y)->number;
  return neo_js_context_create_number(ctx, valx > valy ? valx : valy);
}
NEO_JS_CFUNCTION(neo_js_math_min) {
  if (argc < 2) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double valx = neo_js_variable_to_number(x)->number;
  neo_js_variable_t y = neo_js_context_to_number(ctx, argv[1]);
  NEO_JS_TRY_AND_THROW(y);
  double valy = neo_js_variable_to_number(y)->number;
  return neo_js_context_create_number(ctx, valx > valy ? valy : valx);
}
NEO_JS_CFUNCTION(neo_js_math_pow) {
  if (argc < 2) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double valx = neo_js_variable_to_number(x)->number;
  neo_js_variable_t y = neo_js_context_to_number(ctx, argv[1]);
  NEO_JS_TRY_AND_THROW(y);
  double valy = neo_js_variable_to_number(y)->number;
  return neo_js_context_create_number(ctx, pow(valx, valy));
}
NEO_JS_CFUNCTION(neo_js_math_random) {
  return neo_js_context_create_number(ctx, rand() * 1.0f / RAND_MAX);
}
NEO_JS_CFUNCTION(neo_js_math_round) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, (int64_t)(val + 0.5f));
}
NEO_JS_CFUNCTION(neo_js_math_sign) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  if (isnan(val)) {
    return neo_js_context_create_number(ctx, val);
  }
  double flag = 0;
  if (val > 0) {
    flag = 1;
  }
  if (val < -0.f) {
    flag = -1;
  }
  flag = val;
  return neo_js_context_create_number(ctx, flag);
}
NEO_JS_CFUNCTION(neo_js_math_sin) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, sin(val));
}
NEO_JS_CFUNCTION(neo_js_math_sinh) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, sinh(val));
}
NEO_JS_CFUNCTION(neo_js_math_sqrt) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, sqrt(val));
}
NEO_JS_CFUNCTION(neo_js_math_tan) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, tan(val));
}
NEO_JS_CFUNCTION(neo_js_math_tanh) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, tanh(val));
}
NEO_JS_CFUNCTION(neo_js_math_trunc) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t x = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(x);
  double val = neo_js_variable_to_number(x)->number;
  return neo_js_context_create_number(ctx, trunc(val));
}

void neo_js_context_init_std_math(neo_js_context_t ctx) {
  neo_js_variable_t math =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));

  neo_js_context_set_field(ctx, neo_js_context_get_std(ctx).global,
                           neo_js_context_create_string(ctx, "Math"), math,
                           NULL);

  neo_js_context_set_field(ctx, math, neo_js_context_create_string(ctx, "E"),
                           neo_js_context_create_number(ctx, M_E), NULL);

  neo_js_context_set_field(ctx, math, neo_js_context_create_string(ctx, "LN2"),
                           neo_js_context_create_number(ctx, M_LN2), NULL);

  neo_js_context_set_field(ctx, math, neo_js_context_create_string(ctx, "LN10"),
                           neo_js_context_create_number(ctx, M_LN10), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, "LOG2E"),
                           neo_js_context_create_number(ctx, M_LOG2E), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, "LOG10E"),
                           neo_js_context_create_number(ctx, M_LOG10E), NULL);

  neo_js_context_set_field(ctx, math, neo_js_context_create_string(ctx, "PI"),
                           neo_js_context_create_number(ctx, M_PI), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, "SQRT1_2"),
                           neo_js_context_create_number(ctx, M_SQRT1_2), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, "SQRT2"),
                           neo_js_context_create_number(ctx, M_SQRT2), NULL);

  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "abs"),
      neo_js_context_create_cfunction(ctx, "abs", neo_js_math_abs), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "acos"),
      neo_js_context_create_cfunction(ctx, "acos", neo_js_math_acos), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "acosh"),
      neo_js_context_create_cfunction(ctx, "acosh", neo_js_math_acosh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "asin"),
      neo_js_context_create_cfunction(ctx, "asin", neo_js_math_asin), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "asinh"),
      neo_js_context_create_cfunction(ctx, "asinh", neo_js_math_asinh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "atan"),
      neo_js_context_create_cfunction(ctx, "atan", neo_js_math_atan), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "atan2"),
      neo_js_context_create_cfunction(ctx, "atan2", neo_js_math_atan2), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "atanh"),
      neo_js_context_create_cfunction(ctx, "atanh", neo_js_math_atanh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "cbrt"),
      neo_js_context_create_cfunction(ctx, "cbrt", neo_js_math_cbrt), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "ceil"),
      neo_js_context_create_cfunction(ctx, "ceil", neo_js_math_ceil), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "clz32"),
      neo_js_context_create_cfunction(ctx, "clz32", neo_js_math_clz32), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "cos"),
      neo_js_context_create_cfunction(ctx, "cos", neo_js_math_cos), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "cosh"),
      neo_js_context_create_cfunction(ctx, "cosh", neo_js_math_cosh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "exp"),
      neo_js_context_create_cfunction(ctx, "exp", neo_js_math_exp), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "expm1"),
      neo_js_context_create_cfunction(ctx, "expm1", neo_js_math_expm1), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "floor"),
      neo_js_context_create_cfunction(ctx, "floor", neo_js_math_floor), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "fround"),
      neo_js_context_create_cfunction(ctx, "fround", neo_js_math_fround), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "hypot"),
      neo_js_context_create_cfunction(ctx, "hypot", neo_js_math_hypot), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "imul"),
      neo_js_context_create_cfunction(ctx, "imul", neo_js_math_imul), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "log"),
      neo_js_context_create_cfunction(ctx, "log", neo_js_math_log), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "log1p"),
      neo_js_context_create_cfunction(ctx, "log1p", neo_js_math_log1p), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "log2"),
      neo_js_context_create_cfunction(ctx, "log2", neo_js_math_log2), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "log10"),
      neo_js_context_create_cfunction(ctx, "log10", neo_js_math_log10), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "max"),
      neo_js_context_create_cfunction(ctx, "max", neo_js_math_max), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "min"),
      neo_js_context_create_cfunction(ctx, "min", neo_js_math_min), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "pow"),
      neo_js_context_create_cfunction(ctx, "pow", neo_js_math_pow), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "random"),
      neo_js_context_create_cfunction(ctx, "random", neo_js_math_random), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "round"),
      neo_js_context_create_cfunction(ctx, "round", neo_js_math_round), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "sign"),
      neo_js_context_create_cfunction(ctx, "sign", neo_js_math_sign), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "sin"),
      neo_js_context_create_cfunction(ctx, "sin", neo_js_math_sin), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "sinh"),
      neo_js_context_create_cfunction(ctx, "sinh", neo_js_math_sinh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "sqrt"),
      neo_js_context_create_cfunction(ctx, "sqrt", neo_js_math_sqrt), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "tan"),
      neo_js_context_create_cfunction(ctx, "tan", neo_js_math_tan), true, false,
      true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "tanh"),
      neo_js_context_create_cfunction(ctx, "tanh", neo_js_math_tanh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, "trunc"),
      neo_js_context_create_cfunction(ctx, "trunc", neo_js_math_trunc), true,
      false, true);
}