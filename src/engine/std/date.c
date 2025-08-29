#include "engine/std/date.h"
#include "core/allocator.h"
#include "core/clock.h"
#include "core/unicode.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <wchar.h>

NEO_JS_CFUNCTION(neo_js_date_constructor) {
  if (neo_js_context_get_call_type(ctx) != NEO_JS_CONSTRUCT_CALL) {
    return neo_js_context_construct(
        ctx, neo_js_context_get_std(ctx).date_constructor, 0, NULL);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_time_t *time =
      neo_allocator_alloc(allocator, sizeof(struct _neo_time_t), NULL);
  neo_time_t *utc =
      neo_allocator_alloc(allocator, sizeof(struct _neo_time_t), NULL);
  neo_js_context_set_opaque(ctx, self, L"#time", time);
  neo_js_context_set_opaque(ctx, self, L"#utc", utc);

  for (uint32_t idx = 0; idx < argc; idx++) {
    if (neo_js_variable_get_type(argv[idx])->kind == NEO_JS_TYPE_OBJECT) {
      neo_time_t *time = neo_js_context_get_opaque(ctx, argv[idx], L"#time");
      if (time) {
        continue;
      }
    }
    argv[idx] = neo_js_context_to_primitive(ctx, argv[idx], NULL);
    NEO_JS_TRY_AND_THROW(argv[idx]);
  }

  if (!argc) {
    *time =
        neo_clock_resolve(neo_clock_get_timestamp(), neo_clock_get_timezone());
    *utc = neo_clock_resolve(neo_clock_get_timestamp(), 0);
  } else if (argc == 1) {
    if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_OBJECT) {
      *time = *(neo_time_t *)neo_js_context_get_opaque(ctx, argv[0], L"#time");
      *utc = *(neo_time_t *)neo_js_context_get_opaque(ctx, argv[0], L"#utc");
      return neo_js_context_create_undefined(ctx);
    }
    if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_STRING) {
      int64_t timestamp = 0;
      int64_t timezone = neo_clock_get_timezone();
      if (neo_clock_parse_iso(neo_js_variable_to_string(argv[0])->string,
                              &timestamp)) {
        *time = neo_clock_resolve(timestamp, timezone);
        *utc = neo_clock_resolve(timestamp, 0);
        return neo_js_context_create_undefined(ctx);
      } else if (neo_clock_parse_rfc(neo_js_variable_to_string(argv[0])->string,
                                     &timestamp)) {
        *time = neo_clock_resolve(timestamp, timezone);
        *utc = neo_clock_resolve(timestamp, 0);
        return neo_js_context_create_undefined(ctx);
      }
    }
    argv[0] = neo_js_context_to_number(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(argv[0]);
    if (isnan(neo_js_variable_to_number(argv[0])->number)) {
      time->invalid = true;
      utc->invalid = true;
    } else {
      int64_t timestamp = neo_js_variable_to_number(argv[0])->number;
      *time = neo_clock_resolve(timestamp, neo_clock_get_timezone());
      *utc = neo_clock_resolve(timestamp, 0);
    }
  } else {
    neo_js_variable_t v_year = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(v_year);
    neo_js_variable_t v_month = neo_js_context_to_integer(ctx, argv[1]);
    NEO_JS_TRY_AND_THROW(v_month);
    neo_js_variable_t v_day = NULL;
    if (argc > 2) {
      v_day = neo_js_context_to_integer(ctx, argv[2]);
      NEO_JS_TRY_AND_THROW(v_day);
    }
    neo_js_variable_t v_hours = NULL;
    if (argc > 3) {
      v_hours = neo_js_context_to_integer(ctx, argv[3]);
      NEO_JS_TRY_AND_THROW(v_hours);
    }
    neo_js_variable_t v_minutes = NULL;
    if (argc > 4) {
      v_minutes = neo_js_context_to_integer(ctx, argv[4]);
      NEO_JS_TRY_AND_THROW(v_minutes);
    }
    neo_js_variable_t v_seconds = NULL;
    if (argc > 5) {
      v_seconds = neo_js_context_to_integer(ctx, argv[5]);
      NEO_JS_TRY_AND_THROW(v_seconds);
    }
    neo_js_variable_t v_milliseconds = NULL;
    if (argc > 6) {
      v_milliseconds = neo_js_context_to_integer(ctx, argv[6]);
      NEO_JS_TRY_AND_THROW(v_milliseconds);
    }
    time->year = neo_js_variable_to_number(v_year)->number;
    if (time->year < 100) {
      time->year += 1970;
    }
    time->month = neo_js_variable_to_number(v_month)->number;
    if (v_day) {
      time->day = neo_js_variable_to_number(v_day)->number - 1;
    } else {
      time->day = 0;
    }
    if (v_hours) {
      time->hour = neo_js_variable_to_number(v_hours)->number;
    } else {
      time->hour = 0;
    }
    if (v_minutes) {
      time->minute = neo_js_variable_to_number(v_minutes)->number;
    } else {
      time->minute = 0;
    }
    if (v_seconds) {
      time->second = neo_js_variable_to_number(v_seconds)->number;
    } else {
      time->second = 0;
    }
    if (v_milliseconds) {
      time->millisecond = neo_js_variable_to_number(v_milliseconds)->number;
    } else {
      time->millisecond = 0;
    }
    time->timezone = neo_clock_get_timezone();
    neo_clock_format(time);
    *utc = neo_clock_resolve(time->timestamp, 0);
  }
  return neo_js_context_create_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_date_now) {
  int64_t now = neo_clock_get_timestamp();
  return neo_js_context_create_number(ctx, now);
}

NEO_JS_CFUNCTION(neo_js_date_parse) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_js_variable_t source = neo_js_context_to_string(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(source);
  int64_t timestamp = 0;
  if (neo_clock_parse_iso(neo_js_variable_to_string(source)->string,
                          &timestamp)) {
    return neo_js_context_create_number(ctx, timestamp);
  } else if (neo_clock_parse_rfc(neo_js_variable_to_string(source)->string,
                                 &timestamp)) {
    return neo_js_context_create_number(ctx, timestamp);
  } else {
    return neo_js_context_create_number(ctx, NAN);
  }
}

NEO_JS_CFUNCTION(neo_js_date_utc) {
  if (!argc) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t time = {0};
  neo_js_variable_t v_year = neo_js_context_to_integer(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(v_year);
  neo_js_variable_t v_month = NULL;
  if (argc > 1) {
    v_month = neo_js_context_to_integer(ctx, argv[1]);
    NEO_JS_TRY_AND_THROW(v_month);
  }
  neo_js_variable_t v_day = NULL;
  if (argc > 2) {
    v_day = neo_js_context_to_integer(ctx, argv[2]);
    NEO_JS_TRY_AND_THROW(v_day);
  }
  neo_js_variable_t v_hours = NULL;
  if (argc > 3) {
    v_hours = neo_js_context_to_integer(ctx, argv[3]);
    NEO_JS_TRY_AND_THROW(v_hours);
  }
  neo_js_variable_t v_minutes = NULL;
  if (argc > 4) {
    v_minutes = neo_js_context_to_integer(ctx, argv[4]);
    NEO_JS_TRY_AND_THROW(v_minutes);
  }
  neo_js_variable_t v_seconds = NULL;
  if (argc > 5) {
    v_seconds = neo_js_context_to_integer(ctx, argv[5]);
    NEO_JS_TRY_AND_THROW(v_seconds);
  }
  neo_js_variable_t v_milliseconds = NULL;
  if (argc > 6) {
    v_milliseconds = neo_js_context_to_integer(ctx, argv[6]);
    NEO_JS_TRY_AND_THROW(v_milliseconds);
  }
  if (v_year) {
    time.year = neo_js_variable_to_number(v_year)->number;
    if (time.year < 100) {
      time.year += 1970;
    }
  } else {
    time.year = 0;
  }
  if (v_month) {
    time.month = neo_js_variable_to_number(v_month)->number;
  } else {
    time.month = 0;
  }
  if (v_day) {
    time.day = neo_js_variable_to_number(v_day)->number - 1;
  } else {
    time.day = 0;
  }
  if (v_hours) {
    time.hour = neo_js_variable_to_number(v_hours)->number;
  } else {
    time.hour = 0;
  }
  if (v_minutes) {
    time.minute = neo_js_variable_to_number(v_minutes)->number;
  } else {
    time.minute = 0;
  }
  if (v_seconds) {
    time.second = neo_js_variable_to_number(v_seconds)->number;
  } else {
    time.second = 0;
  }
  if (v_milliseconds) {
    time.millisecond = neo_js_variable_to_number(v_milliseconds)->number;
  } else {
    time.millisecond = 0;
  }
  neo_clock_format(&time);
  return neo_js_context_create_number(ctx, time.timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_get_date) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->day + 1);
}
NEO_JS_CFUNCTION(neo_js_date_get_day) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->weakday);
}
NEO_JS_CFUNCTION(neo_js_date_get_full_year) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->year);
}
NEO_JS_CFUNCTION(neo_js_date_get_hours) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->hour);
}
NEO_JS_CFUNCTION(neo_js_date_get_milliseconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->millisecond);
}
NEO_JS_CFUNCTION(neo_js_date_get_minutes) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->minute);
}
NEO_JS_CFUNCTION(neo_js_date_get_month) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->month + 1);
}
NEO_JS_CFUNCTION(neo_js_date_get_seconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->second);
}
NEO_JS_CFUNCTION(neo_js_date_get_time) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->timestamp);
}

NEO_JS_CFUNCTION(neo_js_date_get_timezone_offset) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->timezone);
}

NEO_JS_CFUNCTION(neo_js_date_get_utc_date) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->day + 1);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_day) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->weakday);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_full_year) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->year);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_hours) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->hour);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_milliseconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->millisecond);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_minutes) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->minute);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_month) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->month + 1);
}
NEO_JS_CFUNCTION(neo_js_date_get_utc_seconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->second);
}
NEO_JS_CFUNCTION(neo_js_date_get_year) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->year - 1970);
}
NEO_JS_CFUNCTION(neo_js_date_set_date) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t date = NULL;
  if (argc > 0) {
    date = argv[0];
  } else {
    date = neo_js_context_create_undefined(ctx);
  }
  date = neo_js_context_to_integer(ctx, date);
  NEO_JS_TRY_AND_THROW(date);
  int64_t d = neo_js_variable_to_number(date)->number;
  tm->day = d - 1;
  neo_clock_format(tm);
  *utc = neo_clock_resolve(tm->timestamp, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_full_year) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  tm->year = v;
  neo_clock_format(tm);
  *utc = neo_clock_resolve(tm->timestamp, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_hours) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  tm->hour = v;
  neo_clock_format(tm);
  *utc = neo_clock_resolve(tm->timestamp, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_milliseconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  tm->millisecond = v;
  neo_clock_format(tm);
  *utc = neo_clock_resolve(tm->timestamp, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_minutes) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  tm->minute = v;
  neo_clock_format(tm);
  *utc = neo_clock_resolve(tm->timestamp, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_month) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  tm->month = v - 1;
  neo_clock_format(tm);
  *utc = neo_clock_resolve(tm->timestamp, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_seconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  tm->second = v;
  neo_clock_format(tm);
  *utc = neo_clock_resolve(tm->timestamp, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_time) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  neo_clock_format(tm);
  *tm = neo_clock_resolve(v, tm->timezone);
  *utc = neo_clock_resolve(v, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_date) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  utc->day = v - 1;
  neo_clock_format(utc);
  *tm = neo_clock_resolve(utc->timestamp, tm->timestamp);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_full_year) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  utc->year = v;
  neo_clock_format(utc);
  *tm = neo_clock_resolve(utc->timestamp, tm->timestamp);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_hours) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  utc->hour = v;
  neo_clock_format(utc);
  *tm = neo_clock_resolve(utc->timestamp, tm->timestamp);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_milliseconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  utc->millisecond = v;
  neo_clock_format(utc);
  *tm = neo_clock_resolve(utc->timestamp, tm->timestamp);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_minutes) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  utc->minute = v;
  neo_clock_format(utc);
  *tm = neo_clock_resolve(utc->timestamp, tm->timestamp);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_month) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  utc->month = v - 1;
  neo_clock_format(utc);
  *tm = neo_clock_resolve(utc->timestamp, tm->timestamp);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_utc_seconds) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  utc->second = v;
  neo_clock_format(utc);
  *tm = neo_clock_resolve(utc->timestamp, tm->timestamp);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_set_year) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  val = neo_js_context_to_integer(ctx, val);
  NEO_JS_TRY_AND_THROW(val);
  int64_t v = neo_js_variable_to_number(val)->number;
  tm->year = v - 1970;
  neo_clock_format(tm);
  *utc = neo_clock_resolve(tm->timestamp, 0);
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_to_date_string) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  static const wchar_t *week_names[] = {
      L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat",
  };
  static const wchar_t *month_names[] = {
      L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
      L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec",
  };
  wchar_t result[1024];
  swprintf(result, 1024, L"%ls,%ls %d %d", week_names[tm->weakday],
           month_names[tm->month], tm->day + 1, tm->year);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_to_iso_string) {
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (utc->invalid) {
    return neo_js_context_create_string(ctx, L"Invalid Date");
  }
  wchar_t result[32];
  swprintf(result, 32, L"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc->year,
           utc->month + 1, utc->day + 1, utc->hour, utc->minute, utc->second,
           utc->millisecond);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_to_json) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_string(ctx, L"Invalid Date");
  }
  wchar_t result[32];
  swprintf(result, 32, L"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm->year,
           tm->month + 1, tm->day + 1, tm->hour, tm->minute, tm->second,
           tm->millisecond);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_to_local_date_string);
NEO_JS_CFUNCTION(neo_js_date_to_local_string);
NEO_JS_CFUNCTION(neo_js_date_to_local_time_string);
NEO_JS_CFUNCTION(neo_js_date_to_string) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  static const wchar_t *week_names[] = {
      L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat",
  };
  static const wchar_t *month_names[] = {
      L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
      L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec",
  };
  struct tm *local_time = NULL;
  time_t raw_time;
  char timezone_name[100];
  timezone_name[0] = 0;
  time(&raw_time);
  local_time = localtime(&raw_time);
  if (local_time != NULL) {
    strftime(timezone_name, 100, "GMT%z (%Z)", local_time);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  wchar_t *zone = neo_string_to_wstring(allocator, timezone_name);
  neo_js_context_defer_free(ctx, zone);
  wchar_t result[1024];
  swprintf(result, 1024, L"%ls, %02d %ls %d %02d:%02d:%02d %ls",
           week_names[tm->weakday], tm->day + 1, month_names[tm->month],
           tm->year, tm->hour, tm->minute, tm->second, zone);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_to_time_string) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  struct tm *local_time = NULL;
  time_t raw_time;
  char timezone_name[100];
  timezone_name[0] = 0;
  time(&raw_time);
  local_time = localtime(&raw_time);
  if (local_time != NULL) {
    strftime(timezone_name, 100, "GMT%z (%Z)", local_time);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  wchar_t *zone = neo_string_to_wstring(allocator, timezone_name);
  neo_js_context_defer_free(ctx, zone);
  wchar_t result[128];
  swprintf(result, 128, L"%02d:%02d:%02d %ls", tm->hour, tm->minute, tm->second,
           zone);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_to_utc_string) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  static const wchar_t *week_names[] = {
      L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat",
  };
  static const wchar_t *month_names[] = {
      L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
      L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec",
  };
  wchar_t result[1024];
  swprintf(result, 1024, L"%ls, %02d %ls %d %02d:%02d:%02d GMT",
           week_names[tm->weakday], tm->day + 1, month_names[tm->month],
           tm->year, tm->hour, tm->minute, tm->second);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_value_of) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_to_primitive) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"Invalid hint: undefined");
  }
  neo_js_variable_t hint = neo_js_context_to_string(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(hint);
  const wchar_t *hint_string = neo_js_variable_to_string(hint)->string;
  if (wcscmp(hint_string, L"default") == 0 ||
      wcscmp(hint_string, L"string") == 0) {
    return neo_js_date_to_string(ctx, self, 0, NULL);
  } else if (wcscmp(hint_string, L"number") == 0) {
    return neo_js_date_value_of(ctx, self, 0, NULL);
  } else {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    size_t len = wcslen(hint_string);
    wchar_t *message =
        neo_allocator_alloc(allocator, sizeof(wchar_t) * (len + 16), NULL);
    neo_js_context_defer_free(ctx, message);
    swprintf(message, len + 16, L"Invalid hint: %ls", hint_string);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
  }
}

void neo_js_context_init_std_date(neo_js_context_t ctx) {
  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).date_constructor, neo_js_context_create_string(ctx, L"now"),
      neo_js_context_create_cfunction(ctx, L"now", neo_js_date_now), true,
      false, true);

  neo_js_variable_t parse =
      neo_js_context_create_cfunction(ctx, L"parse", neo_js_date_parse);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).date_constructor,
                           neo_js_context_create_string(ctx, L"parse"), parse,
                           true, false, true);

  neo_js_variable_t utc =
      neo_js_context_create_cfunction(ctx, L"UTC", neo_js_date_utc);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).date_constructor,
                           neo_js_context_create_string(ctx, L"UTC"), utc, true,
                           false, true);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).date_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t get_date =
      neo_js_context_create_cfunction(ctx, L"getDate", neo_js_date_get_date);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getDate"),
                           get_date, true, false, true);

  neo_js_variable_t get_day =
      neo_js_context_create_cfunction(ctx, L"getDay", neo_js_date_get_day);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getDay"),
                           get_day, true, false, true);

  neo_js_variable_t get_full_year = neo_js_context_create_cfunction(
      ctx, L"getFullYear", neo_js_date_get_full_year);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getFullYear"),
                           get_full_year, true, false, true);

  neo_js_variable_t get_hours =
      neo_js_context_create_cfunction(ctx, L"getHours", neo_js_date_get_hours);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getHours"),
                           get_hours, true, false, true);

  neo_js_variable_t get_milliseconds = neo_js_context_create_cfunction(
      ctx, L"getMilliseconds", neo_js_date_get_milliseconds);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"getMilliseconds"),
      get_milliseconds, true, false, true);

  neo_js_variable_t get_minutes = neo_js_context_create_cfunction(
      ctx, L"getMinutes", neo_js_date_get_minutes);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getMinutes"),
                           get_minutes, true, false, true);

  neo_js_variable_t get_month =
      neo_js_context_create_cfunction(ctx, L"getMonth", neo_js_date_get_month);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getMonth"),
                           get_month, true, false, true);

  neo_js_variable_t get_seconds = neo_js_context_create_cfunction(
      ctx, L"getSeconds", neo_js_date_get_seconds);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getSeconds"),
                           get_seconds, true, false, true);

  neo_js_variable_t get_time =
      neo_js_context_create_cfunction(ctx, L"getTime", neo_js_date_get_time);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getTime"),
                           get_time, true, false, true);

  neo_js_variable_t get_timezone_offset = neo_js_context_create_cfunction(
      ctx, L"getTimezoneOffset", neo_js_date_get_timezone_offset);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"getTimezoneOffset"),
      get_timezone_offset, true, false, true);

  neo_js_variable_t get_utc_date = neo_js_context_create_cfunction(
      ctx, L"getUTCDate", neo_js_date_get_utc_date);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getUTCDate"),
                           get_utc_date, true, false, true);

  neo_js_variable_t get_utc_day = neo_js_context_create_cfunction(
      ctx, L"getUTCDay", neo_js_date_get_utc_day);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getUTCDay"),
                           get_utc_day, true, false, true);

  neo_js_variable_t get_utc_full_year = neo_js_context_create_cfunction(
      ctx, L"getUTCFullYear", neo_js_date_get_utc_full_year);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getUTCFullYear"),
                           get_utc_full_year, true, false, true);

  neo_js_variable_t get_utc_hours = neo_js_context_create_cfunction(
      ctx, L"getUTCHours", neo_js_date_get_utc_hours);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getUTCHours"),
                           get_utc_hours, true, false, true);

  neo_js_variable_t get_utc_milliseconds = neo_js_context_create_cfunction(
      ctx, L"getUTCMilliseconds", neo_js_date_get_utc_milliseconds);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"getUTCMilliseconds"),
      get_utc_milliseconds, true, false, true);

  neo_js_variable_t get_utc_minutes = neo_js_context_create_cfunction(
      ctx, L"getUTCMinutes", neo_js_date_get_utc_minutes);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getUTCMinutes"),
                           get_utc_minutes, true, false, true);

  neo_js_variable_t get_utc_month = neo_js_context_create_cfunction(
      ctx, L"getUTCMonth", neo_js_date_get_utc_month);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getUTCMonth"),
                           get_utc_month, true, false, true);

  neo_js_variable_t get_utc_seconds = neo_js_context_create_cfunction(
      ctx, L"getUTCSeconds", neo_js_date_get_utc_seconds);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"getUTCSeconds"),
                           get_utc_seconds, true, false, true);

  neo_js_variable_t set_date =
      neo_js_context_create_cfunction(ctx, L"setDate", neo_js_date_set_date);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setDate"),
                           set_date, true, false, true);

  neo_js_variable_t set_full_year = neo_js_context_create_cfunction(
      ctx, L"setFullYear", neo_js_date_set_full_year);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setFullYear"),
                           set_full_year, true, false, true);

  neo_js_variable_t set_utc_date = neo_js_context_create_cfunction(
      ctx, L"setUTCDate", neo_js_date_set_utc_date);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setUTCDate"),
                           set_utc_date, true, false, true);

  neo_js_variable_t set_utc_full_year = neo_js_context_create_cfunction(
      ctx, L"setUTCFullYear", neo_js_date_set_utc_full_year);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setUTCFullYear"),
                           set_utc_full_year, true, false, true);

  neo_js_variable_t set_utc_hours = neo_js_context_create_cfunction(
      ctx, L"setUTCHours", neo_js_date_set_utc_hours);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setUTCHours"),
                           set_utc_hours, true, false, true);

  neo_js_variable_t set_utc_milliseconds = neo_js_context_create_cfunction(
      ctx, L"setUTCMilliseconds", neo_js_date_set_utc_milliseconds);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"setUTCMilliseconds"),
      set_utc_milliseconds, true, false, true);

  neo_js_variable_t set_utc_minutes = neo_js_context_create_cfunction(
      ctx, L"setUTCMinutes", neo_js_date_set_utc_minutes);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setUTCMinutes"),
                           set_utc_minutes, true, false, true);

  neo_js_variable_t set_utc_month = neo_js_context_create_cfunction(
      ctx, L"setUTCMonth", neo_js_date_set_utc_month);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setUTCMonth"),
                           set_utc_month, true, false, true);

  neo_js_variable_t set_utc_seconds = neo_js_context_create_cfunction(
      ctx, L"setUTCSeconds", neo_js_date_set_utc_seconds);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setUTCSeconds"),
                           set_utc_seconds, true, false, true);

  neo_js_variable_t set_year =
      neo_js_context_create_cfunction(ctx, L"setYear", neo_js_date_set_year);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"setYear"),
                           set_year, true, false, true);

  neo_js_variable_t to_time_string = neo_js_context_create_cfunction(
      ctx, L"toTimeString", neo_js_date_to_time_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toTimeString"),
                           to_time_string, true, false, true);

  neo_js_variable_t to_string =
      neo_js_context_create_cfunction(ctx, L"toString", neo_js_date_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           to_string, true, false, true);

  neo_js_variable_t to_date_string = neo_js_context_create_cfunction(
      ctx, L"toDateString", neo_js_date_to_date_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toDateString"),
                           to_date_string, true, false, true);

  neo_js_variable_t to_iso_string = neo_js_context_create_cfunction(
      ctx, L"toISOString", neo_js_date_to_iso_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toISOString"),
                           to_iso_string, true, false, true);

  neo_js_variable_t to_utc_string = neo_js_context_create_cfunction(
      ctx, L"toUTCString", neo_js_date_to_utc_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toUTCString"),
                           to_utc_string, true, false, true);

  neo_js_variable_t value_of =
      neo_js_context_create_cfunction(ctx, L"valueOf", neo_js_date_value_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"valueOf"),
                           value_of, true, false, true);

  neo_js_variable_t to_primitive = neo_js_context_create_cfunction(
      ctx, L"[Symbol.toPrimitive]", neo_js_date_to_primitive);
  neo_js_context_def_field(
      ctx, prototype,
      neo_js_context_get_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor,
          neo_js_context_create_string(ctx, L"toPrimitive"), NULL),
      to_primitive, true, false, true);
}