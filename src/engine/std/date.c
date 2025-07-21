#include "engine/std/date.h"
#include "core/clock.h"
#include "engine/context.h"
NEO_JS_CFUNCTION(neo_js_date_constructor) {
  return neo_js_context_create_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_date_now) {
  int64_t now = neo_clock_get_timestamp();
  return neo_js_context_create_number(ctx, now);
}

NEO_JS_CFUNCTION(neo_js_date_parse);

NEO_JS_CFUNCTION(neo_js_date_utc);
NEO_JS_CFUNCTION(neo_js_date_get_date);
NEO_JS_CFUNCTION(neo_js_date_get_day);
NEO_JS_CFUNCTION(neo_js_date_get_full_year);
NEO_JS_CFUNCTION(neo_js_date_get_hours);
NEO_JS_CFUNCTION(neo_js_date_get_milliseconds);
NEO_JS_CFUNCTION(neo_js_date_get_minutes);
NEO_JS_CFUNCTION(neo_js_date_get_month);
NEO_JS_CFUNCTION(neo_js_date_get_seconds);
NEO_JS_CFUNCTION(neo_js_date_get_time);
NEO_JS_CFUNCTION(neo_js_date_get_timezone_offset) {
  int64_t timestamp = neo_clock_get_timezone();
  return neo_js_context_create_number(ctx, timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_date);
NEO_JS_CFUNCTION(neo_js_date_get_utc_day);
NEO_JS_CFUNCTION(neo_js_date_get_utc_full_year);
NEO_JS_CFUNCTION(neo_js_date_get_utc_hours);
NEO_JS_CFUNCTION(neo_js_date_get_utc_milliseconds);
NEO_JS_CFUNCTION(neo_js_date_get_utc_minutes);
NEO_JS_CFUNCTION(neo_js_date_get_utc_month);
NEO_JS_CFUNCTION(neo_js_date_get_utc_seconds);
NEO_JS_CFUNCTION(neo_js_date_get_year);
NEO_JS_CFUNCTION(neo_js_date_set_date);
NEO_JS_CFUNCTION(neo_js_date_set_full_year);
NEO_JS_CFUNCTION(neo_js_date_set_hours);
NEO_JS_CFUNCTION(neo_js_date_set_milliseconds);
NEO_JS_CFUNCTION(neo_js_date_set_minutes);
NEO_JS_CFUNCTION(neo_js_date_set_month);
NEO_JS_CFUNCTION(neo_js_date_set_seconds);
NEO_JS_CFUNCTION(neo_js_date_set_time);
NEO_JS_CFUNCTION(neo_js_date_set_utc_date);
NEO_JS_CFUNCTION(neo_js_date_set_utc_full_year);
NEO_JS_CFUNCTION(neo_js_date_set_utc_hours);
NEO_JS_CFUNCTION(neo_js_date_set_utc_milliseconds);
NEO_JS_CFUNCTION(neo_js_date_set_utc_minutes);
NEO_JS_CFUNCTION(neo_js_date_set_utc_month);
NEO_JS_CFUNCTION(neo_js_date_set_utc_seconds);
NEO_JS_CFUNCTION(neo_js_date_set_year);
NEO_JS_CFUNCTION(neo_js_date_to_date_string);
NEO_JS_CFUNCTION(neo_js_date_to_iso_string);
NEO_JS_CFUNCTION(neo_js_date_to_json);
NEO_JS_CFUNCTION(neo_js_date_to_local_date_string);
NEO_JS_CFUNCTION(neo_js_date_to_local_string);
NEO_JS_CFUNCTION(neo_js_date_to_local_time_string);
NEO_JS_CFUNCTION(neo_js_date_to_string);
NEO_JS_CFUNCTION(neo_js_date_to_time_string);
NEO_JS_CFUNCTION(neo_js_date_to_utc_string);
NEO_JS_CFUNCTION(neo_js_date_value_of);
NEO_JS_CFUNCTION(neo_js_date_to_primitive);