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