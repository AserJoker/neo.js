#ifndef _H_NEO_RUNTIME_DATE_
#define _H_NEO_RUNTIME_DATE_
#include "neojs/engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_date_constructor);
NEO_JS_CFUNCTION(neo_js_date_now);
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
NEO_JS_CFUNCTION(neo_js_date_get_timezone_offset);
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
NEO_JS_CFUNCTION(neo_js_date_to_locale_date_string);
NEO_JS_CFUNCTION(neo_js_date_to_locale_string);
NEO_JS_CFUNCTION(neo_js_date_to_locale_time_string);
NEO_JS_CFUNCTION(neo_js_date_to_string);
NEO_JS_CFUNCTION(neo_js_date_value_of);
NEO_JS_CFUNCTION(neo_js_date_to_time_string);
NEO_JS_CFUNCTION(neo_js_date_to_utc_string);
NEO_JS_CFUNCTION(neo_js_date_to_primitive);
void neo_initialize_js_date(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif