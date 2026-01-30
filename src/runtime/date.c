#include "neojs/runtime/date.h"
#include "neojs/core/allocator.h"
#include "neojs/core/clock.h"
#include "neojs/core/string.h"
#include "neojs/engine/context.h"
#include "neojs/engine/number.h"
#include "neojs/engine/string.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
#include <stdint.h>
#include <string.h>
NEO_JS_CFUNCTION(neo_js_date_constructor) {
  if (neo_js_context_get_type(ctx) == NEO_JS_CONTEXT_CONSTRUCT) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_time_t *time = neo_allocator_alloc(allocator, sizeof(neo_time_t), NULL);
    memset(time, 0, sizeof(neo_time_t));
    if (!argc) {
      int64_t timestamp = neo_clock_get_timestamp();
      *time = neo_clock_resolve(timestamp, neo_clock_get_timezone());
      neo_js_variable_set_opaque(self, ctx, "date", time);
      return self;
    } else if (argc == 1) {
      if (argv[0]->value->type >= NEO_JS_TYPE_OBJECT) {
        neo_time_t *ts = neo_js_variable_get_opaque(argv[0], ctx, "date");
        if (ts) {
          *time = *ts;
          neo_js_variable_set_opaque(self, ctx, "date", time);
          return self;
        }
      }
      neo_js_variable_t value =
          neo_js_variable_to_primitive(argv[0], ctx, "default");
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, time);
        return value;
      }
      if (value->value->type == NEO_JS_TYPE_STRING) {
        const uint16_t *src = ((neo_js_string_t)value->value)->value;
        char *str = neo_string16_to_string(allocator, src);
        int64_t timestamp = 0;
        if (!neo_clock_parse_iso(str, &timestamp) &&
            !neo_clock_parse_rfc(str, &timestamp)) {
          time->invalid = true;
        }
        neo_allocator_free(allocator, str);
        neo_js_variable_set_opaque(self, ctx, "date", time);
        return self;
      }
      value = neo_js_variable_to_integer(value, ctx);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, time);
        return value;
      }
      double val = ((neo_js_number_t)value->value)->value;
      if (val >= ((int64_t)2 << 53) || val <= ((int64_t)2 << 53)) {
        time->invalid = false;
      }
      neo_js_variable_set_opaque(self, ctx, "date", time);
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
        neo_allocator_free(allocator, time);
        return value;
      }
      year = ((neo_js_number_t)value->value)->value;
      value = neo_js_context_get_argument(ctx, argc, argv, 1);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, time);
        return value;
      }
      monthIndex = ((neo_js_number_t)value->value)->value;
      if (argc > 2) {
        value = neo_js_variable_to_integer(argv[2], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, time);
          return value;
        }
        day = ((neo_js_number_t)value->value)->value;
      }
      if (argc > 3) {
        value = neo_js_variable_to_integer(argv[3], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, time);
          return value;
        }
        hours = ((neo_js_number_t)value->value)->value;
      }
      if (argc > 4) {
        value = neo_js_variable_to_integer(argv[3], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, time);
          return value;
        }
        minutes = ((neo_js_number_t)value->value)->value;
      }
      if (argc > 5) {
        value = neo_js_variable_to_integer(argv[3], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, time);
          return value;
        }
        seconds = ((neo_js_number_t)value->value)->value;
      }
      if (argc > 5) {
        value = neo_js_variable_to_integer(argv[3], ctx);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          neo_allocator_free(allocator, time);
          return value;
        }
        milliseconds = ((neo_js_number_t)value->value)->value;
      }
      if (year < -100000000 || year > 100000000) {
        time->invalid = true;
        neo_js_variable_set_opaque(self, ctx, "date", time);
        return self;
      }
      if (milliseconds < -100000000 || milliseconds > 100000000) {
        time->invalid = true;
        neo_js_variable_set_opaque(self, ctx, "date", time);
        return self;
      }
      if (hours < 0 || hours > 23) {
        time->invalid = true;
        neo_js_variable_set_opaque(self, ctx, "date", time);
        return self;
      }
      if (minutes < 0 || minutes > 59) {
        time->invalid = true;
        neo_js_variable_set_opaque(self, ctx, "date", time);
        return self;
      }
      if (seconds < 0 || seconds > 59) {
        time->invalid = true;
        neo_js_variable_set_opaque(self, ctx, "date", time);
        return self;
      }
      if (milliseconds < 0 || milliseconds > 999) {
        time->invalid = true;
        neo_js_variable_set_opaque(self, ctx, "date", time);
        return self;
      }
      if (year >= 0 && year <= 99) {
        year += 1900;
      }
      monthIndex += 1;
      time->year = (int64_t)year;
      time->month = (int64_t)monthIndex;
      time->day = (int64_t)day;
      time->hour = (int64_t)hours;
      time->minute = (int64_t)minutes;
      time->second = (int64_t)seconds;
      time->millisecond = (int64_t)milliseconds;
      neo_clock_format(time);
      neo_js_variable_set_opaque(self, ctx, "date", time);
      return self;
    }
  } else {
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t obj =
        neo_js_variable_construct(constant->date_class, ctx, 0, NULL);
    return neo_js_date_to_string(ctx, obj, 0, NULL);
  }
}
static const char *weekday_strings[] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
};
static const char *month_strings[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};
NEO_JS_CFUNCTION(neo_js_date_to_string) {
  neo_time_t *time = neo_js_variable_get_opaque(self, ctx, "date");
  if (!time) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Date.prototype.toString requires that 'this' be a Date");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  char s[128];
  int32_t timezone = neo_clock_get_timezone();
  sprintf(s, "%s %s %2ld %ld %ld:%ld:%ld GMT%+03d%02d",
          weekday_strings[time->weakday - 1], month_strings[time->month],
          time->year, time->day + 1, time->hour, time->minute, time->second,
          timezone / -60, timezone % 60);
  return neo_js_context_create_cstring(ctx, s);
}
void neo_initialize_js_date(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->date_class =
      neo_js_context_create_cfunction(ctx, neo_js_date_constructor, "Date");
  neo_js_variable_t prototype = neo_js_variable_get_field(
      constant->date_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, prototype, "toString", neo_js_date_to_string);
}