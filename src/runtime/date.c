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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unicode/ucal.h>
#include <unicode/utypes.h>

NEO_JS_CFUNCTION(neo_js_date_constructor) {
  if (neo_js_context_get_type(ctx) == NEO_JS_CONTEXT_CONSTRUCT) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    if (!argc) {
      neo_js_variable_t timestamp =
          neo_js_context_create_number(ctx, neo_clock_get_timestamp());
      neo_js_variable_set_internal(self, ctx, "timestamp", timestamp);
      return self;
    } else if (argc == 1) {
      if (argv[0]->value->type >= NEO_JS_TYPE_OBJECT) {
        neo_js_variable_t ts =
            neo_js_variable_get_internal(argv[0], ctx, "timestamp");
        if (ts && ts->value->type == NEO_JS_TYPE_NUMBER) {
          neo_js_variable_t timestamp = neo_js_context_create_number(
              ctx, ((neo_js_number_t)ts->value)->value);
          neo_js_variable_set_internal(self, ctx, "timestamp", timestamp);
          return self;
        }
      }
      neo_js_variable_t value =
          neo_js_variable_to_primitive(argv[0], ctx, "default");
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      if (value->value->type == NEO_JS_TYPE_STRING) {
        neo_js_constant_t constant = neo_js_context_get_constant(ctx);
        neo_js_variable_t timestamp =
            neo_js_date_parse(ctx, constant->date_class, argc, argv);
        if (timestamp->value->type == NEO_JS_TYPE_EXCEPTION) {
          return timestamp;
        }
        neo_js_variable_set_internal(self, ctx, "timestamp", timestamp);
        return self;
      }
      value = neo_js_variable_to_integer(value, ctx);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      double val = ((neo_js_number_t)value->value)->value;
      neo_js_variable_t timestamp = NULL;
      if (val >= ((int64_t)2 << 53) || val <= -((int64_t)2 << 53)) {
        timestamp = neo_js_context_create_number(ctx, NAN);
      } else {
        timestamp = neo_js_context_create_number(ctx, (int64_t)val);
      }
      neo_js_variable_set_internal(self, ctx, "timestamp", timestamp);
      return self;
    } else {
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t timestamp =
          neo_js_date_utc(ctx, constant->date_class, argc, argv);
      neo_js_variable_set_internal(self, ctx, "timestamp", timestamp);
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
      u"yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", u"yyyy-MM-dd'T'HH:mm:ss'Z'",
      u"EEE MMM dd yyyy HH:mm:ss",     u"MMM dd yyyy HH:mm:ss",
      u"MMM, dd yyyy HH:mm:ss",        NULL,
  };
  static const UChar *tzs[] = {
      u"UTC", u"UTC", NULL, NULL, NULL, NULL,
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
NEO_JS_CFUNCTION(neo_js_date_get_date) {

  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getDate requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t day = ucal_get(cal, UCAL_DAY_OF_MONTH, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, day);
}
NEO_JS_CFUNCTION(neo_js_date_get_day) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getDay requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t day = ucal_get(cal, UCAL_DAY_OF_WEEK, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, day - 1);
}
NEO_JS_CFUNCTION(neo_js_date_get_full_year) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getFullYear requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t year = ucal_get(cal, UCAL_YEAR, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, year);
}
NEO_JS_CFUNCTION(neo_js_date_get_hours) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getHours requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t hour = ucal_get(cal, UCAL_HOUR_OF_DAY, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, hour);
}
NEO_JS_CFUNCTION(neo_js_date_get_milliseconds) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getMilliseconds requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t hour = ucal_get(cal, UCAL_MILLISECOND, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, hour);
}
NEO_JS_CFUNCTION(neo_js_date_get_minutes) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getMinutes requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t minute = ucal_get(cal, UCAL_MINUTE, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, minute);
}
NEO_JS_CFUNCTION(neo_js_date_get_month) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getMonth requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t month = ucal_get(cal, UCAL_MONTH, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, month);
}
NEO_JS_CFUNCTION(neo_js_date_get_seconds) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getSeconds requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t second = ucal_get(cal, UCAL_SECOND, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, second);
}
NEO_JS_CFUNCTION(neo_js_date_get_time) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getTime requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_get_timezone_offset) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getTimezoneOffset requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  int32_t timezone = neo_clock_get_timezone();
  return neo_js_context_create_number(ctx, -timezone);
}

NEO_JS_CFUNCTION(neo_js_date_get_utc_date) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getUTCDate requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t day = ucal_get(cal, UCAL_DAY_OF_MONTH, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, day);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_day) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getUTCDay requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t day = ucal_get(cal, UCAL_DAY_OF_WEEK, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, day - 1);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_full_year) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getUTCFullYear requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t year = ucal_get(cal, UCAL_YEAR, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, year);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_hours) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getUTCHours requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t hour = ucal_get(cal, UCAL_HOUR_OF_DAY, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, hour);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_milliseconds) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx,
        "Date.prototype.getUTCMilliseconds requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t hour = ucal_get(cal, UCAL_MILLISECOND, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, hour);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_minutes) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getUTCMinutes requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t minute = ucal_get(cal, UCAL_MINUTE, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, minute);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_month) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getUTCMonth requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t month = ucal_get(cal, UCAL_MONTH, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, month);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_seconds) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getUTCSeconds requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t second = ucal_get(cal, UCAL_SECOND, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, second);
}
NEO_JS_CFUNCTION(neo_js_date_get_year) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.getYear requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  int32_t year = ucal_get(cal, UCAL_YEAR, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  return neo_js_context_create_number(ctx, year - 1900);
}

NEO_JS_CFUNCTION(neo_js_date_set_date) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setDate requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_DAY_OF_MONTH, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_full_year) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setFullYear requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_YEAR, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_hours) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setHours requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_HOUR_OF_DAY, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_milliseconds) {

  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setMilliseconds requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_MILLISECOND, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_minutes) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setMinutes requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_MINUTE, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_month) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setMonth requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_MONTH, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_seconds) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setSeconds requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_SECOND, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_time) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setTime requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  ((neo_js_number_t)ts->value)->value = value;
  return neo_js_context_create_number(ctx, value);
}

NEO_JS_CFUNCTION(neo_js_date_set_utc_date) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setDate requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_DAY_OF_MONTH, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_full_year) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setFullYear requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_YEAR, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_hours) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setHours requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_HOUR_OF_DAY, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_milliseconds) {

  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setMilliseconds requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_MILLISECOND, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_minutes) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setMinutes requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_MINUTE, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_month) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setMonth requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_MONTH, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_seconds) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setUTCSeconds requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(u"UTC", -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_SECOND, value);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_year) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.setYear requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
  arg = neo_js_variable_to_number(arg, ctx);
  if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
    return arg;
  }
  double value = ((neo_js_number_t)arg->value)->value;
  if (isnan(value)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  UErrorCode status = 0;
  UCalendar *cal = ucal_open(NULL, -1, "en", UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_setMillis(cal, (int64_t)timestamp, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_set(cal, UCAL_YEAR, value + 1900);
  timestamp = ucal_getMillis(cal, &status);
  if (U_FAILURE(status)) {
    ucal_close(cal);
    ((neo_js_number_t)ts->value)->value = NAN;
    return neo_js_context_create_number(ctx, NAN);
  }
  ucal_close(cal);
  ((neo_js_number_t)ts->value)->value = timestamp;
  return neo_js_context_create_number(ctx, timestamp);
}

NEO_JS_CFUNCTION(neo_js_date_to_date_string) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toDateString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *s = neo_clock_to_rfc(allocator, (int64_t)timestamp);
  if (s) {
    neo_js_variable_t res = neo_js_context_create_cstring(ctx, s);
    neo_allocator_free(allocator, s);
    return res;
  } else {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
}
NEO_JS_CFUNCTION(neo_js_date_to_iso_string) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toISOString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *s = neo_clock_to_iso(allocator, (int64_t)timestamp);
  if (s) {
    neo_js_variable_t res = neo_js_context_create_cstring(ctx, s);
    neo_allocator_free(allocator, s);
    return res;
  } else {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
}
NEO_JS_CFUNCTION(neo_js_date_to_json) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toJSON requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *s = neo_clock_to_iso(allocator, (int64_t)timestamp);
  if (s) {
    neo_js_variable_t res = neo_js_context_create_cstring(ctx, s);
    neo_allocator_free(allocator, s);
    return res;
  } else {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
}
NEO_JS_CFUNCTION(neo_js_date_to_locale_date_string) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toLocalDateString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t date_time_format = neo_js_variable_get_field(
      constant->intl, ctx,
      neo_js_context_create_string(ctx, u"DateTimeFormat"));
  if (date_time_format->value->type == NEO_JS_TYPE_EXCEPTION) {
    return date_time_format;
  }
  neo_js_variable_t fmt =
      neo_js_variable_construct(date_time_format, ctx, argc, argv);
  if (fmt->value->type == NEO_JS_TYPE_EXCEPTION) {
    return fmt;
  }
  neo_js_variable_t format = neo_js_variable_get_field(
      fmt, ctx, neo_js_context_create_string(ctx, u"format"));
  if (format->value->type == NEO_JS_TYPE_EXCEPTION) {
    return format;
  }
  return neo_js_variable_call(format, ctx, fmt, 1, &self);
}
NEO_JS_CFUNCTION(neo_js_date_to_locale_string) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toLocalDateString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t date_time_format = neo_js_variable_get_field(
      constant->intl, ctx,
      neo_js_context_create_string(ctx, u"DateTimeFormat"));
  if (date_time_format->value->type == NEO_JS_TYPE_EXCEPTION) {
    return date_time_format;
  }
  neo_js_variable_t fmt =
      neo_js_variable_construct(date_time_format, ctx, argc, argv);
  if (fmt->value->type == NEO_JS_TYPE_EXCEPTION) {
    return fmt;
  }
  neo_js_variable_t format = neo_js_variable_get_field(
      fmt, ctx, neo_js_context_create_string(ctx, u"format"));
  if (format->value->type == NEO_JS_TYPE_EXCEPTION) {
    return format;
  }
  return neo_js_variable_call(format, ctx, fmt, 1, &self);
}
NEO_JS_CFUNCTION(neo_js_date_to_locale_time_string) {

  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toLocalDateString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t date_time_format = neo_js_variable_get_field(
      constant->intl, ctx,
      neo_js_context_create_string(ctx, u"DateTimeFormat"));
  if (date_time_format->value->type == NEO_JS_TYPE_EXCEPTION) {
    return date_time_format;
  }
  neo_js_variable_t fmt =
      neo_js_variable_construct(date_time_format, ctx, argc, argv);
  if (fmt->value->type == NEO_JS_TYPE_EXCEPTION) {
    return fmt;
  }
  neo_js_variable_t format = neo_js_variable_get_field(
      fmt, ctx, neo_js_context_create_string(ctx, u"format"));
  if (format->value->type == NEO_JS_TYPE_EXCEPTION) {
    return format;
  }
  return neo_js_variable_call(format, ctx, fmt, 1, &self);
}

NEO_JS_CFUNCTION(neo_js_date_to_string) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *s = neo_clock_to_rfc(allocator, (int64_t)timestamp);
  if (s) {
    neo_js_variable_t res = neo_js_context_create_cstring(ctx, s);
    neo_allocator_free(allocator, s);
    return res;
  } else {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
}

NEO_JS_CFUNCTION(neo_js_date_value_of) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.valueOf requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_to_time_string) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.to requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
  UChar result[256];
  if (!neo_clock_to_string(timestamp, u"HH:mm:ss 'GMP'Z (zzzz)", NULL, result,
                           256)) {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_to_utc_string) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toUTCString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
  UChar result[256];
  if (!neo_clock_to_string(timestamp, u"EEE, dd MMM yyyy HH:mm:ss 'GMP'Z", NULL,
                           result, 256)) {
    return neo_js_context_create_string(ctx, u"Invalid Date");
  }
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_to_primitive) {
  neo_js_variable_t ts = neo_js_variable_get_internal(self, ctx, "timestamp");
  if (!ts || ts->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx,
        "Date.prototype[Symbol.toPrimitive] requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double timestamp = ((neo_js_number_t)ts->value)->value;
  if (isnan(timestamp)) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, timestamp);
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
  NEO_JS_DEF_METHOD(ctx, prototype, "getDate", neo_js_date_get_date);
  NEO_JS_DEF_METHOD(ctx, prototype, "getDay", neo_js_date_get_day);
  NEO_JS_DEF_METHOD(ctx, prototype, "getFullYear", neo_js_date_get_full_year);
  NEO_JS_DEF_METHOD(ctx, prototype, "getHours", neo_js_date_get_hours);
  NEO_JS_DEF_METHOD(ctx, prototype, "getMilliseconds",
                    neo_js_date_get_milliseconds);
  NEO_JS_DEF_METHOD(ctx, prototype, "getMinutes", neo_js_date_get_minutes);
  NEO_JS_DEF_METHOD(ctx, prototype, "getMonth", neo_js_date_get_month);
  NEO_JS_DEF_METHOD(ctx, prototype, "getSeconds", neo_js_date_get_seconds);
  NEO_JS_DEF_METHOD(ctx, prototype, "getTime", neo_js_date_get_time);
  NEO_JS_DEF_METHOD(ctx, prototype, "getTimezoneOffset",
                    neo_js_date_get_timezone_offset);
  NEO_JS_DEF_METHOD(ctx, prototype, "getUTCDate", neo_js_date_get_utc_date);
  NEO_JS_DEF_METHOD(ctx, prototype, "getUTCDay", neo_js_date_get_utc_day);
  NEO_JS_DEF_METHOD(ctx, prototype, "getUTCFullYear",
                    neo_js_date_get_utc_full_year);
  NEO_JS_DEF_METHOD(ctx, prototype, "getUTCHours", neo_js_date_get_utc_hours);
  NEO_JS_DEF_METHOD(ctx, prototype, "getUTCMilliseconds",
                    neo_js_date_get_utc_milliseconds);
  NEO_JS_DEF_METHOD(ctx, prototype, "getUTCMinutes",
                    neo_js_date_get_utc_minutes);
  NEO_JS_DEF_METHOD(ctx, prototype, "getUTCMonth", neo_js_date_get_utc_month);
  NEO_JS_DEF_METHOD(ctx, prototype, "getUTCSeconds",
                    neo_js_date_get_utc_seconds);
  NEO_JS_DEF_METHOD(ctx, prototype, "getYear", neo_js_date_get_year);

  NEO_JS_DEF_METHOD(ctx, prototype, "setDate", neo_js_date_set_date);
  NEO_JS_DEF_METHOD(ctx, prototype, "setFullYear", neo_js_date_set_full_year);
  NEO_JS_DEF_METHOD(ctx, prototype, "setHours", neo_js_date_set_hours);
  NEO_JS_DEF_METHOD(ctx, prototype, "setMilliseconds",
                    neo_js_date_set_milliseconds);
  NEO_JS_DEF_METHOD(ctx, prototype, "setMinutes", neo_js_date_set_minutes);
  NEO_JS_DEF_METHOD(ctx, prototype, "setMonth", neo_js_date_set_month);
  NEO_JS_DEF_METHOD(ctx, prototype, "setSeconds", neo_js_date_set_seconds);
  NEO_JS_DEF_METHOD(ctx, prototype, "setTime", neo_js_date_set_time);
  NEO_JS_DEF_METHOD(ctx, prototype, "setUTCDate", neo_js_date_set_utc_date);
  NEO_JS_DEF_METHOD(ctx, prototype, "setUTCFullYear",
                    neo_js_date_set_utc_full_year);
  NEO_JS_DEF_METHOD(ctx, prototype, "setUTCHours", neo_js_date_set_utc_hours);
  NEO_JS_DEF_METHOD(ctx, prototype, "setUTCMilliseconds",
                    neo_js_date_set_utc_milliseconds);
  NEO_JS_DEF_METHOD(ctx, prototype, "setUTCMinutes",
                    neo_js_date_set_utc_minutes);
  NEO_JS_DEF_METHOD(ctx, prototype, "setUTCMonth", neo_js_date_set_utc_month);
  NEO_JS_DEF_METHOD(ctx, prototype, "setUTCSeconds",
                    neo_js_date_set_utc_seconds);
  NEO_JS_DEF_METHOD(ctx, prototype, "setYear", neo_js_date_set_year);
  NEO_JS_DEF_METHOD(ctx, prototype, "toDateString", neo_js_date_to_date_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "toISOString", neo_js_date_to_iso_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "toJSON", neo_js_date_to_json);
  NEO_JS_DEF_METHOD(ctx, prototype, "toLocaleDateString",
                    neo_js_date_to_locale_date_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "toLocaleString",
                    neo_js_date_to_locale_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "toLocaleTimeString",
                    neo_js_date_to_locale_time_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "toString", neo_js_date_to_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "toTimeString", neo_js_date_to_time_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "toUTCString", neo_js_date_to_utc_string);
  NEO_JS_DEF_METHOD(ctx, prototype, "valueOf", neo_js_date_value_of);
  neo_js_variable_def_field(
      prototype, ctx, constant->symbol_to_primitive,
      neo_js_context_create_cfunction(ctx, neo_js_date_to_primitive,
                                      "[Symbol.toPrimitive]"),
      true, false, true);
}