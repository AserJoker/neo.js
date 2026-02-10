#include "neojs/runtime/date.h"
#include "neojs/core/allocator.h"
#include "neojs/core/clock.h"
#include "neojs/engine/context.h"
#include "neojs/engine/number.h"
#include "neojs/engine/string.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

NEO_JS_CFUNCTION(neo_js_date_constructor) {
  if (neo_js_context_get_type(ctx) == NEO_JS_CONTEXT_CONSTRUCT) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    int64_t *timestamp = neo_allocator_alloc(allocator, sizeof(int64_t), NULL);
    *timestamp = 0;
    if (!argc) {
      *timestamp = neo_clock_get_timestamp();
      neo_js_variable_set_opaque(self, ctx, "timestamp", timestamp);
      return self;
    } else if (argc == 1) {
      if (argv[0]->value->type >= NEO_JS_TYPE_OBJECT) {
        int64_t *ts = neo_js_variable_get_opaque(argv[0], ctx, "timestamp");
        if (ts) {
          *timestamp = *ts;
          neo_js_variable_set_opaque(self, ctx, "timestamp", timestamp);
          return self;
        }
      }
      neo_js_variable_t value =
          neo_js_variable_to_primitive(argv[0], ctx, "default");
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, timestamp);
        return value;
      }
      if (value->value->type == NEO_JS_TYPE_STRING) {
        const uint16_t *src = ((neo_js_string_t)value->value)->value;
        static const UChar *formats[] = {
            u"yyyy-MM-dd'T'HH:mm:ss.SSS'Z'",
            u"yyyy-MM-dd'T'HH:mm:ss'Z'",
            u"EEE MMM dd yyyy HH:mm:ss",
            NULL,
        };
        static const UChar *tzs[] = {
            u"UTC",
            u"UTC",
            NULL,
            NULL,
        };
        for (size_t idx = 0; formats[idx] != NULL; idx++) {
          if (neo_clock_parse(src, formats[idx], timestamp, tzs[idx])) {
            neo_js_variable_set_opaque(self, ctx, "timestamp", timestamp);
            return self;
          }
        }
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return self;
      }
      value = neo_js_variable_to_integer(value, ctx);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, timestamp);
        return value;
      }
      double val = ((neo_js_number_t)value->value)->value;
      if (val >= ((int64_t)2 << 53) || val <= -((int64_t)2 << 53)) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
      } else {
        *timestamp = (int64_t)val;
        neo_js_variable_set_opaque(self, ctx, "timestamp", timestamp);
      }
      return self;
    } else {
      double year = 0;
      double monthIndex = 0;
      double day = 1;
      double hours = 0;
      double minutes = 0;
      double seconds = 0;
      double milliseconds = 0;
      neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
      value = neo_js_variable_to_integer(value, ctx);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return value;
      }
      year = ((neo_js_number_t)value->value)->value;
      value = neo_js_context_get_argument(ctx, argc, argv, 1);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return value;
      }
      monthIndex = ((neo_js_number_t)value->value)->value;
      if (argc > 2) {
        value = neo_js_variable_to_integer(argv[2], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, timestamp);
          neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
          return value;
        }
        day = ((neo_js_number_t)value->value)->value;
      }
      if (argc > 3) {
        value = neo_js_variable_to_integer(argv[3], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, timestamp);
          neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
          return value;
        }
        hours = ((neo_js_number_t)value->value)->value;
      }
      if (argc > 4) {
        value = neo_js_variable_to_integer(argv[4], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, timestamp);
          neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
          return value;
        }
        minutes = ((neo_js_number_t)value->value)->value;
      }
      if (argc > 5) {
        value = neo_js_variable_to_integer(argv[5], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, timestamp);
          neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
          return value;
        }
        seconds = ((neo_js_number_t)value->value)->value;
      }
      if (argc > 6) {
        value = neo_js_variable_to_integer(argv[6], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, timestamp);
          neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
          return value;
        }
        milliseconds = ((neo_js_number_t)value->value)->value;
      }
      if (year < -100000000 || year > 100000000) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return self;
      }
      if (milliseconds < -100000000 || milliseconds > 100000000) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return self;
      }
      if (hours < 0 || hours > 23) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return self;
      }
      if (minutes < 0 || minutes > 59) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return self;
      }
      if (seconds < 0 || seconds > 59) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return self;
      }
      if (milliseconds < 0 || milliseconds > 999) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
        return self;
      }
      if (year >= 0 && year <= 99) {
        year += 1900;
      }
      monthIndex += 1;
      char str[256];
      sprintf(str, "%04d-%02d-%02dT%02d:%02d:%02d.%04dZ", (int32_t)year,
              (int32_t)monthIndex, (int32_t)day, (int32_t)hours,
              (int32_t)minutes, (int32_t)seconds, (int32_t)milliseconds);
      UChar source[256];
      u_austrcpy(source, str);
      if (!neo_clock_parse(source, u"yyyy-MM-dd'T'hh:mm:ss.SSSS'Z'", timestamp,
                           u"UTC")) {
        neo_allocator_free(allocator, timestamp);
        neo_js_variable_set_opaque(self, ctx, "timestamp", NULL);
      } else {
        neo_js_variable_set_opaque(self, ctx, "timestamp", timestamp);
      }
      return self;
    }
  } else {
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t obj =
        neo_js_variable_construct(constant->date_class, ctx, 0, NULL);
    return neo_js_date_to_string(ctx, obj, 0, NULL);
  }
}

NEO_JS_CFUNCTION(neo_js_date_now) {
  return neo_js_context_create_number(ctx, neo_clock_get_timestamp());
}
NEO_JS_CFUNCTION(neo_js_date_parse) {
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_string(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  const uint16_t *str = ((neo_js_string_t)arg->value)->value;
  static const UChar *formats[] = {
      u"yyyy-MM-dd'T'HH:mm:ss.SSS'Z'",
      u"yyyy-MM-dd'T'HH:mm:ss'Z'",
      u"EEE MMM dd yyyy HH:mm:ss",
      NULL,
  };
  static const UChar *tzs[] = {
      u"UTC",
      u"UTC",
      NULL,
      NULL,
  };
  int64_t timestamp = 0;
  for (size_t idx = 0; formats[idx] != NULL; idx++) {
    if (neo_clock_parse(str, formats[idx], &timestamp, tzs[idx])) {
      return neo_js_context_create_number(ctx, timestamp);
    }
  }
  return neo_js_context_create_number(ctx, NAN);
}

NEO_JS_CFUNCTION(neo_js_date_utc) {
  double year = 0;
  double monthIndex = 0;
  double day = 1;
  double hours = 0;
  double minutes = 0;
  double seconds = 0;
  double milliseconds = 0;
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  value = neo_js_variable_to_integer(value, ctx);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value;
  }
  year = ((neo_js_number_t)value->value)->value;
  value = neo_js_context_get_argument(ctx, argc, argv, 1);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value;
  }
  monthIndex = ((neo_js_number_t)value->value)->value;
  if (argc > 2) {
    value = neo_js_variable_to_integer(argv[2], ctx);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    day = ((neo_js_number_t)value->value)->value;
  }
  if (argc > 3) {
    value = neo_js_variable_to_integer(argv[3], ctx);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    hours = ((neo_js_number_t)value->value)->value;
  }
  if (argc > 4) {
    value = neo_js_variable_to_integer(argv[4], ctx);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    minutes = ((neo_js_number_t)value->value)->value;
  }
  if (argc > 5) {
    value = neo_js_variable_to_integer(argv[5], ctx);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    seconds = ((neo_js_number_t)value->value)->value;
  }
  if (argc > 6) {
    value = neo_js_variable_to_integer(argv[6], ctx);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    milliseconds = ((neo_js_number_t)value->value)->value;
  }
  if (year < -100000000 || year > 100000000) {
    return neo_js_context_create_number(ctx, NAN);
  }
  if (milliseconds < -100000000 || milliseconds > 100000000) {
    return self;
  }
  if (hours < 0 || hours > 23) {
    return neo_js_context_create_number(ctx, NAN);
    return self;
  }
  if (minutes < 0 || minutes > 59) {
    return neo_js_context_create_number(ctx, NAN);
  }
  if (seconds < 0 || seconds > 59) {
    return neo_js_context_create_number(ctx, NAN);
  }
  if (milliseconds < 0 || milliseconds > 999) {
    return neo_js_context_create_number(ctx, NAN);
  }
  if (year >= 0 && year <= 99) {
    year += 1900;
  }
  monthIndex += 1;
  char str[256];
  sprintf(str, "%04d-%02d-%02dT%02d:%02d:%02d.%04dZ", (int32_t)year,
          (int32_t)monthIndex, (int32_t)day, (int32_t)hours, (int32_t)minutes,
          (int32_t)seconds, (int32_t)milliseconds);
  UChar source[256];
  u_austrcpy(source, str);
  int64_t timestamp = 0;
  if (neo_clock_parse(source, u"yyyy-MM-dd'T'hh:mm:ss.SSSS'Z'", &timestamp,
                      u"UTC")) {
    return neo_js_context_create_number(ctx, timestamp);
  }
  return neo_js_context_create_number(ctx, NAN);
}

NEO_JS_CFUNCTION(neo_js_date_to_string) {
  if (!neo_js_variable_has_opaque(self, "timestamp")) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  int64_t *timestamp = neo_js_variable_get_opaque(self, ctx, "timestamp");
  if (!timestamp) {
    return neo_js_context_create_cstring(ctx, "Invalid Date");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *s = neo_clock_to_rfc(allocator, *timestamp);
  if (s) {
    neo_js_variable_t res = neo_js_context_create_cstring(ctx, s);
    neo_allocator_free(allocator, s);
    return res;
  } else {
    return neo_js_context_create_cstring(ctx, "Invalid Date");
  }
}

void neo_initialize_js_date(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->date_class =
      neo_js_context_create_cfunction(ctx, neo_js_date_constructor, "Date");
  NEO_JS_DEF_METHOD(ctx, constant->date_class, "now", neo_js_date_now);
  NEO_JS_DEF_METHOD(ctx, constant->date_class, "parse", neo_js_date_parse);
  NEO_JS_DEF_METHOD(ctx, constant->date_class, "UTC", neo_js_date_utc);
  neo_js_variable_t prototype = neo_js_variable_get_field(
      constant->date_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, prototype, "toString", neo_js_date_to_string);
}