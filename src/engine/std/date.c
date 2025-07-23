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
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_time_t *time =
      neo_allocator_alloc(allocator, sizeof(struct _neo_time_t), NULL);
  neo_time_t *utc =
      neo_allocator_alloc(allocator, sizeof(struct _neo_time_t), NULL);
  neo_js_context_set_opaque(ctx, self, L"#time", time);
  neo_js_context_set_opaque(ctx, self, L"#utc", utc);

  for (uint32_t idx = 0; idx < argc; idx++) {
    if (neo_js_variable_get_type(argv[idx])->kind == NEO_TYPE_OBJECT) {
      if (neo_js_variable_to_object(argv[idx])->constructor ==
          neo_js_variable_get_handle(
              neo_js_context_get_date_constructor(ctx))) {
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
    if (neo_js_variable_get_type(argv[0])->kind == NEO_TYPE_NUMBER) {
      int64_t timestamp = neo_js_variable_to_number(argv[0])->number;
      *time = neo_clock_resolve(timestamp, neo_clock_get_timezone());
      *utc = neo_clock_resolve(timestamp, 0);
    } else if (neo_js_variable_get_type(argv[0])->kind == NEO_TYPE_STRING) {
      int64_t timestamp = 0;
      int64_t timezone = 0;
      if (neo_clock_parse_iso(neo_js_variable_to_string(argv[0])->string,
                              &timestamp, &timezone)) {
        *time = neo_clock_resolve(timestamp, timezone);
        *utc = neo_clock_resolve(timestamp, 0);
      } else if (neo_clock_parse_rfc(neo_js_variable_to_string(argv[0])->string,
                                     &timestamp, &timezone)) {
        *time = neo_clock_resolve(timestamp, timezone);
        *utc = neo_clock_resolve(timestamp, 0);
      } else {
        time->invalid = true;
        utc->invalid = true;
      }
    } else {
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

NEO_JS_CFUNCTION(neo_js_date_parse);

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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
NEO_JS_CFUNCTION(neo_js_date_to_date_string);
NEO_JS_CFUNCTION(neo_js_date_to_iso_string) {
  neo_time_t *utc = neo_js_context_get_opaque(ctx, self, L"#utc");
  if (!utc) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
  swprintf(result, 1024, L"%ls,%ls %d %d %d:%d:%d", week_names[tm->weakday],
           month_names[tm->month], tm->day + 1, tm->year, tm->hour, tm->minute,
           tm->second);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_to_time_string) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
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
  swprintf(result, 1024, L"%ls,%ls %d %d %d:%d:%d", week_names[tm->weakday],
           month_names[tm->month], tm->day + 1, tm->year, tm->hour, tm->minute,
           tm->second);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_date_value_of) {
  neo_time_t *tm = neo_js_context_get_opaque(ctx, self, L"#time");
  if (!tm) {
    return neo_js_context_create_simple_error(ctx, NEO_ERROR_TYPE,
                                              L"this is not a Date object.");
  }
  if (tm->invalid) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, tm->timestamp);
}
NEO_JS_CFUNCTION(neo_js_date_to_primitive);