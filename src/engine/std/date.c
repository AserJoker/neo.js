#include "engine/std/date.h"
#include "core/allocator.h"
#include "core/clock.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdint.h>

NEO_JS_CFUNCTION(neo_js_date_constructor) {
  if (!argc) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_time_t *tm =
        neo_allocator_alloc(allocator, sizeof(struct _neo_time_t), NULL);
    *tm =
        neo_clock_resolve(neo_clock_get_timestamp(), neo_clock_get_timezone());
    neo_js_context_set_opaque(ctx, self, L"#time", tm);
  } else {
    for (uint32_t idx = 0; idx < argc; idx++) {
      if (neo_js_variable_get_type(argv[idx])->kind == NEO_TYPE_OBJECT) {
        if (neo_js_variable_to_object(argv[idx])->constructor ==
            neo_js_variable_get_handle(
                neo_js_context_get_date_constructor(ctx))) {
          continue;
        }
      }
      argv[idx] = neo_js_context_to_primitive(ctx, argv[idx], NULL);
    }
    if (argc == 1 &&
        neo_js_variable_get_type(argv[0])->kind == NEO_TYPE_NUMBER) {
      int64_t timestamp = neo_js_variable_to_number(argv[0])->number;
      neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
      neo_time_t *tm =
          neo_allocator_alloc(allocator, sizeof(struct _neo_time_t), NULL);
      *tm = neo_clock_resolve(timestamp, neo_clock_get_timezone());
      neo_js_context_set_opaque(ctx, self, L"#time", tm);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_date_now) {
  int64_t now = neo_clock_get_timestamp();
  neo_time_t time = neo_clock_resolve(now, neo_clock_get_timezone());
  return neo_js_context_create_number(ctx, now);
}

NEO_JS_CFUNCTION(neo_js_date_parse);
NEO_JS_CFUNCTION(neo_js_date_utc);
NEO_JS_CFUNCTION(neo_js_date_get_date) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->day);
}
NEO_JS_CFUNCTION(neo_js_date_get_day) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->weakday);
}
NEO_JS_CFUNCTION(neo_js_date_get_full_year) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->year);
}
NEO_JS_CFUNCTION(neo_js_date_get_hours) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->hour);
}
NEO_JS_CFUNCTION(neo_js_date_get_milliseconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->millisecond);
}
NEO_JS_CFUNCTION(neo_js_date_get_minutes) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->minute);
}
NEO_JS_CFUNCTION(neo_js_date_get_month) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->month);
}
NEO_JS_CFUNCTION(neo_js_date_get_seconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->second);
}
NEO_JS_CFUNCTION(neo_js_date_get_time) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->timestamp);
}

NEO_JS_CFUNCTION(neo_js_date_get_timezone_offset) {
  int64_t timezone = neo_clock_get_timezone();
  return neo_js_context_create_number(ctx, timezone);
}

NEO_JS_CFUNCTION(neo_js_date_get_utc_date);
NEO_JS_CFUNCTION(neo_js_date_get_utc_day);
NEO_JS_CFUNCTION(neo_js_date_get_utc_full_year);
NEO_JS_CFUNCTION(neo_js_date_get_utc_hours);
NEO_JS_CFUNCTION(neo_js_date_get_utc_milliseconds);
NEO_JS_CFUNCTION(neo_js_date_get_utc_minutes);
NEO_JS_CFUNCTION(neo_js_date_get_utc_month);
NEO_JS_CFUNCTION(neo_js_date_get_utc_seconds);
NEO_JS_CFUNCTION(neo_js_date_get_year) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->year - 1970);
}
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
NEO_JS_CFUNCTION(neo_js_date_value_of) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_to_primitive);