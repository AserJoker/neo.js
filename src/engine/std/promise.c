#include "engine/std/promise.h"
#include "core/allocator.h"
#include "core/list.h"
#include "engine/basetype/callable.h"
#include "engine/basetype/error.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/std/array.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdint.h>

typedef struct _neo_js_promise_t *neo_js_promise_t;
struct _neo_js_promise_t {
  enum {
    NEO_PROMISE_PENDDING,
    NEO_PROMISE_FULFILLED,
    NEO_PROMISE_REJECTED
  } status;
  neo_list_t on_fulfilled_callbacks;
  neo_list_t on_rejected_callbacks;
  neo_js_handle_t value;
  neo_js_handle_t error;
  neo_js_context_t ctx;
};

static NEO_JS_CFUNCTION(neo_js_promise_all_on_fulfilled) {
  neo_js_variable_t result = neo_js_context_load_variable(ctx, "result");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, "resolve");
  neo_js_variable_t max = neo_js_context_load_variable(ctx, "max");
  neo_js_variable_t value = argv[0];
  neo_js_array_push(ctx, result, 1, &value);
  if (neo_js_variable_to_number(max)->number != 0) {
    neo_js_variable_t length =
        neo_js_context_get_string_field(ctx, result, "length");
    if (neo_js_variable_to_number(max)->number ==
        neo_js_variable_to_number(length)->number) {
      neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                          &result);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

static NEO_JS_CFUNCTION(neo_js_promise_all_callback) {
  neo_js_variable_t promises = neo_js_context_load_variable(ctx, "promises");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, promises,
      neo_js_context_get_string_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor, "iterator"),
      NULL);
  NEO_JS_TRY(iterator) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, iterator);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  iterator = neo_js_context_call(ctx, iterator, promises, 0, NULL);
  NEO_JS_TRY(iterator) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, iterator);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t next =
      neo_js_context_get_string_field(ctx, iterator, "next");
  NEO_JS_TRY(next) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, next);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t error = neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "variable is not iterable");
    error = neo_js_error_get_error(ctx, error);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_js_variable_t on_fulfilled = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_promise_all_on_fulfilled);
  neo_js_callable_set_closure(ctx, on_fulfilled, "result", result);
  neo_js_callable_set_closure(ctx, on_fulfilled, "resolve", resolve);
  neo_js_variable_t vmax = neo_js_context_create_number(ctx, 0);
  neo_js_callable_set_closure(ctx, on_fulfilled, "max", vmax);
  int64_t max = 0;
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, iterator, 0, NULL);
    NEO_JS_TRY(res) {
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &res);
      return neo_js_context_create_undefined(ctx);
    }
    if (neo_js_variable_get_type(res)->kind != NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t error = neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0, "variable is not iterable");
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &error);
      return neo_js_context_create_undefined(ctx);
    }
    neo_js_variable_t done = neo_js_context_get_string_field(ctx, res, "done");
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t promise =
        neo_js_context_get_string_field(ctx, res, "value");
    NEO_JS_TRY_AND_THROW(promise);
    if (neo_js_context_is_thenable(ctx, promise)) {
      neo_js_variable_t then =
          neo_js_context_get_string_field(ctx, promise, "then");
      NEO_JS_TRY_AND_THROW(then);
      neo_js_variable_t args[] = {on_fulfilled, reject};
      NEO_JS_TRY_AND_THROW(neo_js_context_call(ctx, then, promise, 2, args));
    } else {
      neo_js_array_push(ctx, result, 1, &promise);
    }
    max++;
  }
  neo_js_variable_t length =
      neo_js_context_get_string_field(ctx, result, "length");
  if (neo_js_variable_to_number(length)->number == max) {
    neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                        &result);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_to_number(vmax)->number = max;
  return neo_js_context_create_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_all) {
  neo_js_variable_t callback =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_promise_all_callback);
  if (argc) {
    neo_js_callable_set_closure(ctx, callback, "promises", argv[0]);
  } else {
    neo_js_callable_set_closure(ctx, callback, "promises",
                                neo_js_context_create_undefined(ctx));
  }
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}

static NEO_JS_CFUNCTION(neo_js_promise_all_settled_on_fulfilled) {
  neo_js_variable_t result = neo_js_context_load_variable(ctx, "result");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, "resolve");
  neo_js_variable_t done = neo_js_context_load_variable(ctx, "done");
  neo_js_variable_t idx = neo_js_context_load_variable(ctx, "idx");
  neo_js_variable_t max = neo_js_context_load_variable(ctx, "max");
  neo_js_variable_t value = argv[0];
  neo_js_variable_t obj = neo_js_context_create_object(ctx, NULL);
  neo_js_context_set_field(
      ctx, obj, neo_js_context_create_string(ctx, "status"),
      neo_js_context_create_string(ctx, "fulfilled"), NULL);
  neo_js_context_set_field(ctx, obj, neo_js_context_create_string(ctx, "value"),
                           value, NULL);
  neo_js_context_set_field(ctx, result, idx, obj, NULL);
  neo_js_variable_to_number(done)->number += 1;
  if (neo_js_variable_to_number(max)->number != 0) {
    if (neo_js_variable_to_number(max)->number ==
        neo_js_variable_to_number(done)->number) {
      neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                          &result);
    }
  }
  return neo_js_context_create_undefined(ctx);
}
static NEO_JS_CFUNCTION(neo_js_promise_all_settled_on_rejected) {
  neo_js_variable_t result = neo_js_context_load_variable(ctx, "result");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, "resolve");
  neo_js_variable_t done = neo_js_context_load_variable(ctx, "done");
  neo_js_variable_t idx = neo_js_context_load_variable(ctx, "idx");
  neo_js_variable_t max = neo_js_context_load_variable(ctx, "max");
  neo_js_variable_t value = argv[0];
  neo_js_variable_t obj = neo_js_context_create_object(ctx, NULL);
  neo_js_context_set_field(ctx, obj,
                           neo_js_context_create_string(ctx, "status"),
                           neo_js_context_create_string(ctx, "rejected"), NULL);
  neo_js_context_set_field(
      ctx, obj, neo_js_context_create_string(ctx, "reason"), value, NULL);
  neo_js_context_set_field(ctx, result, idx, obj, NULL);
  neo_js_variable_to_number(done)->number += 1;
  if (neo_js_variable_to_number(max)->number != 0) {
    if (neo_js_variable_to_number(max)->number ==
        neo_js_variable_to_number(done)->number) {
      neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                          &result);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

static NEO_JS_CFUNCTION(neo_js_promise_all_settled_callback) {
  neo_js_variable_t promises = neo_js_context_load_variable(ctx, "promises");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, promises,
      neo_js_context_get_string_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor, "iterator"),
      NULL);
  NEO_JS_TRY(iterator) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, iterator);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  iterator = neo_js_context_call(ctx, iterator, promises, 0, NULL);
  NEO_JS_TRY(iterator) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, iterator);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t next =
      neo_js_context_get_string_field(ctx, iterator, "next");
  NEO_JS_TRY(next) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, next);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t error = neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "variable is not iterable");
    error = neo_js_error_get_error(ctx, error);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_js_variable_t vmax = neo_js_context_create_number(ctx, 0);
  neo_js_variable_t vdone = neo_js_context_create_number(ctx, 0);
  int64_t max = 0;
  int64_t idx = 0;
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, iterator, 0, NULL);
    NEO_JS_TRY(res) {
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &res);
      return neo_js_context_create_undefined(ctx);
    }
    if (neo_js_variable_get_type(res)->kind != NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t error = neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0, "variable is not iterable");
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &error);
      return neo_js_context_create_undefined(ctx);
    }
    neo_js_variable_t done = neo_js_context_get_string_field(ctx, res, "done");
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t promise =
        neo_js_context_get_string_field(ctx, res, "value");
    NEO_JS_TRY_AND_THROW(promise);
    neo_js_variable_t vidx = neo_js_context_create_number(ctx, idx);
    if (neo_js_context_is_thenable(ctx, promise)) {
      neo_js_variable_t on_fulfilled = neo_js_context_create_cfunction(
          ctx, NULL, neo_js_promise_all_settled_on_fulfilled);
      neo_js_callable_set_closure(ctx, on_fulfilled, "result", result);
      neo_js_callable_set_closure(ctx, on_fulfilled, "resolve", resolve);
      neo_js_callable_set_closure(ctx, on_fulfilled, "max", vmax);
      neo_js_callable_set_closure(ctx, on_fulfilled, "done", vdone);
      neo_js_callable_set_closure(ctx, on_fulfilled, "idx", vidx);

      neo_js_variable_t on_rejected = neo_js_context_create_cfunction(
          ctx, NULL, neo_js_promise_all_settled_on_rejected);
      neo_js_callable_set_closure(ctx, on_rejected, "result", result);
      neo_js_callable_set_closure(ctx, on_rejected, "resolve", resolve);
      neo_js_callable_set_closure(ctx, on_rejected, "max", vmax);
      neo_js_callable_set_closure(ctx, on_rejected, "done", vdone);
      neo_js_callable_set_closure(ctx, on_rejected, "idx", vidx);

      neo_js_variable_t then =
          neo_js_context_get_string_field(ctx, promise, "then");
      NEO_JS_TRY_AND_THROW(then);
      neo_js_variable_t args[] = {on_fulfilled, on_rejected};
      NEO_JS_TRY_AND_THROW(neo_js_context_call(ctx, then, promise, 2, args));
    } else {
      neo_js_variable_t obj = neo_js_context_create_object(ctx, NULL);
      neo_js_context_set_field(
          ctx, obj, neo_js_context_create_string(ctx, "status"),
          neo_js_context_create_string(ctx, "fulfilled"), NULL);
      neo_js_context_set_field(
          ctx, obj, neo_js_context_create_string(ctx, "value"), promise, NULL);
      neo_js_context_set_field(ctx, result, vidx, obj, NULL);
      neo_js_variable_to_number(vdone)->number += 1;
    }
    idx++;
    max++;
  }
  if (neo_js_variable_to_number(vdone)->number == max) {
    neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                        &result);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_to_number(vmax)->number = max;
  return neo_js_context_create_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_all_settled) {
  neo_js_variable_t callback = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_promise_all_settled_callback);
  if (argc) {
    neo_js_callable_set_closure(ctx, callback, "promises", argv[0]);
  } else {
    neo_js_callable_set_closure(ctx, callback, "promises",
                                neo_js_context_create_undefined(ctx));
  }
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}

static NEO_JS_CFUNCTION(neo_js_promise_any_on_rejected) {
  neo_js_variable_t result = neo_js_context_load_variable(ctx, "result");
  neo_js_variable_t reject = neo_js_context_load_variable(ctx, "reject");
  neo_js_variable_t done = neo_js_context_load_variable(ctx, "done");
  neo_js_variable_t idx = neo_js_context_load_variable(ctx, "idx");
  neo_js_variable_t max = neo_js_context_load_variable(ctx, "max");
  neo_js_variable_t value = argv[0];
  neo_js_variable_t obj = neo_js_context_create_object(ctx, NULL);
  neo_js_context_set_field(ctx, result, idx, value, NULL);
  neo_js_variable_to_number(done)->number += 1;
  if (neo_js_variable_to_number(max)->number != 0) {
    if (neo_js_variable_to_number(max)->number ==
        neo_js_variable_to_number(done)->number) {
      neo_js_variable_t error = neo_js_context_construct(
          ctx, neo_js_context_get_std(ctx).aggregate_error_constructor, 1,
          &result);
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &error);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

static NEO_JS_CFUNCTION(neo_js_promise_any_callback) {
  neo_js_variable_t promises = neo_js_context_load_variable(ctx, "promises");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, promises,
      neo_js_context_get_string_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor, "iterator"),
      NULL);
  NEO_JS_TRY(iterator) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, iterator);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  iterator = neo_js_context_call(ctx, iterator, promises, 0, NULL);
  NEO_JS_TRY(iterator) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, iterator);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t next =
      neo_js_context_get_string_field(ctx, iterator, "next");
  NEO_JS_TRY(next) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, next);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t error = neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "variable is not iterable");
    error = neo_js_error_get_error(ctx, error);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_js_variable_t vmax = neo_js_context_create_number(ctx, 0);
  neo_js_variable_t vdone = neo_js_context_create_number(ctx, 0);
  int64_t max = 0;
  int64_t idx = 0;
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, iterator, 0, NULL);
    NEO_JS_TRY(res) {
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &res);
      return neo_js_context_create_undefined(ctx);
    }
    if (neo_js_variable_get_type(res)->kind != NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t error = neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0, "variable is not iterable");
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &error);
      return neo_js_context_create_undefined(ctx);
    }
    neo_js_variable_t done = neo_js_context_get_string_field(ctx, res, "done");
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t promise =
        neo_js_context_get_string_field(ctx, res, "value");
    NEO_JS_TRY_AND_THROW(promise);
    neo_js_variable_t vidx = neo_js_context_create_number(ctx, idx);
    if (neo_js_context_is_thenable(ctx, promise)) {

      neo_js_variable_t on_rejected = neo_js_context_create_cfunction(
          ctx, NULL, neo_js_promise_any_on_rejected);
      neo_js_callable_set_closure(ctx, on_rejected, "result", result);
      neo_js_callable_set_closure(ctx, on_rejected, "reject", reject);
      neo_js_callable_set_closure(ctx, on_rejected, "max", vmax);
      neo_js_callable_set_closure(ctx, on_rejected, "done", vdone);
      neo_js_callable_set_closure(ctx, on_rejected, "idx", vidx);

      neo_js_variable_t then =
          neo_js_context_get_string_field(ctx, promise, "then");
      NEO_JS_TRY_AND_THROW(then);
      neo_js_variable_t args[] = {resolve, on_rejected};
      NEO_JS_TRY_AND_THROW(neo_js_context_call(ctx, then, promise, 2, args));
    } else {
      neo_js_variable_t obj = neo_js_context_create_object(ctx, NULL);
      neo_js_context_set_field(
          ctx, obj, neo_js_context_create_string(ctx, "status"),
          neo_js_context_create_string(ctx, "fulfilled"), NULL);
      neo_js_context_set_field(
          ctx, obj, neo_js_context_create_string(ctx, "value"), promise, NULL);
      neo_js_context_set_field(ctx, result, vidx, obj, NULL);
      neo_js_variable_to_number(vdone)->number += 1;
    }
    idx++;
    max++;
  }
  if (neo_js_variable_to_number(vdone)->number == max) {
    neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                        &result);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_to_number(vmax)->number = max;
  return neo_js_context_create_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_any) {
  neo_js_variable_t callback =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_promise_any_callback);
  if (argc) {
    neo_js_callable_set_closure(ctx, callback, "promises", argv[0]);
  } else {
    neo_js_callable_set_closure(ctx, callback, "promises",
                                neo_js_context_create_undefined(ctx));
  }
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}

static NEO_JS_CFUNCTION(neo_js_promise_race_callback) {
  neo_js_variable_t promises = neo_js_context_load_variable(ctx, "promises");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, promises,
      neo_js_context_get_string_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor, "iterator"),
      NULL);
  NEO_JS_TRY(iterator) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, iterator);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  iterator = neo_js_context_call(ctx, iterator, promises, 0, NULL);
  NEO_JS_TRY(iterator) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, iterator);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t next =
      neo_js_context_get_string_field(ctx, iterator, "next");
  NEO_JS_TRY(next) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, next);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t error = neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "variable is not iterable");
    error = neo_js_error_get_error(ctx, error);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &error);
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, iterator, 0, NULL);
    NEO_JS_TRY(res) {
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &res);
      return neo_js_context_create_undefined(ctx);
    }
    if (neo_js_variable_get_type(res)->kind != NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t error = neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0, "variable is not iterable");
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &error);
      return neo_js_context_create_undefined(ctx);
    }
    neo_js_variable_t done = neo_js_context_get_string_field(ctx, res, "done");
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t promise =
        neo_js_context_get_string_field(ctx, res, "value");
    NEO_JS_TRY_AND_THROW(promise);
    if (neo_js_context_is_thenable(ctx, promise)) {
      neo_js_variable_t then =
          neo_js_context_get_string_field(ctx, promise, "then");
      NEO_JS_TRY_AND_THROW(then);
      neo_js_variable_t args[] = {resolve, reject};
      NEO_JS_TRY_AND_THROW(neo_js_context_call(ctx, then, promise, 2, args));
    } else {
      neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                          &promise);
      break;
    }
  }
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_promise_race) {
  neo_js_variable_t callback =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_promise_race_callback);
  if (argc) {
    neo_js_callable_set_closure(ctx, callback, "promises", argv[0]);
  } else {
    neo_js_callable_set_closure(ctx, callback, "promises",
                                neo_js_context_create_undefined(ctx));
  }
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}

static neo_js_variable_t
neo_js_promise_resolve_callback(neo_js_context_t ctx, neo_js_variable_t self,
                                uint32_t argc, neo_js_variable_t *argv) {
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t value = neo_js_context_load_variable(ctx, "value");
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
    value = neo_js_error_get_error(ctx, value);
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &value);
  } else {
    neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                        &value);
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_promise_resolve(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t value = NULL;
  if (argc) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t callback = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_promise_resolve_callback);
  neo_js_callable_set_closure(ctx, callback, "value", value);
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}

neo_js_variable_t neo_js_promise_reject(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_variable_t value = NULL;
  if (argc) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t error = neo_js_context_create_error(ctx, value);
  neo_js_variable_t callback = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_promise_resolve_callback);
  neo_js_callable_set_closure(ctx, callback, "value", error);
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}
NEO_JS_CFUNCTION(neo_js_promise_try) {
  neo_js_variable_t func = NULL;
  if (argc) {
    func = argv[0];
  } else {
    func = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_context_call(
      ctx, func, neo_js_context_create_undefined(ctx), argc - 1, &argv[1]);
  NEO_JS_TRY(result) {
    neo_js_variable_t error = neo_js_error_get_error(ctx, result);
    return neo_js_promise_reject(
        ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &error);
  }
  return neo_js_promise_resolve(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &result);
}
static NEO_JS_CFUNCTION(neo_js_promise_with_resolvers_callback) {
  neo_js_variable_t result = neo_js_context_load_variable(ctx, "result");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_context_set_field(ctx, resolve,
                           neo_js_context_create_string(ctx, "resolve"),
                           resolve, NULL);
  neo_js_context_set_field(
      ctx, resolve, neo_js_context_create_string(ctx, "reject"), reject, NULL);
  return neo_js_context_create_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_with_resolvers) {
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_js_variable_t callback = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_promise_with_resolvers_callback);
  neo_js_callable_set_closure(ctx, callback, "result", result);
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}

NEO_JS_CFUNCTION(neo_js_promise_species) { return self; }

static void neo_js_promise_dispose(neo_allocator_t allocator,
                                   neo_js_promise_t promise) {
  neo_allocator_free(allocator, promise->on_fulfilled_callbacks);
  neo_allocator_free(allocator, promise->on_rejected_callbacks);
}
static neo_js_promise_t neo_create_js_promise(neo_allocator_t allocator,
                                              neo_js_context_t ctx) {
  neo_js_promise_t promise = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_promise_t), neo_js_promise_dispose);
  promise->status = NEO_PROMISE_PENDDING;
  promise->on_fulfilled_callbacks = neo_create_list(allocator, NULL);
  promise->on_rejected_callbacks = neo_create_list(allocator, NULL);
  promise->value = NULL;
  promise->error = NULL;
  promise->ctx = ctx;
  return promise;
}

static neo_js_variable_t neo_js_resolve(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_promise_t promise =
      neo_js_context_get_opaque(ctx, self, "[[promise]]");
  if (promise->status == NEO_PROMISE_PENDDING) {
    neo_js_variable_t resolve =
        neo_js_context_get_internal(ctx, self, "[[resolve]]");
    neo_js_variable_t reject =
        neo_js_context_get_internal(ctx, self, "[[reject]]");
    neo_js_variable_t value = NULL;
    if (argc) {
      value = argv[0];
    } else {
      value = neo_js_context_create_undefined(ctx);
    }
    if (neo_js_context_is_thenable(ctx, value)) {
      neo_js_variable_t then =
          neo_js_context_get_string_field(ctx, value, "then");
      neo_js_variable_t argv[] = {resolve, reject};
      neo_js_variable_t error = neo_js_context_call(ctx, then, value, 2, argv);
      if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
        return error;
      }
    } else {
      promise->status = NEO_PROMISE_FULFILLED;
      promise->value = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(promise->value,
                               neo_js_variable_get_handle(self));
      for (neo_list_node_t it =
               neo_list_get_first(promise->on_fulfilled_callbacks);
           it != neo_list_get_tail(promise->on_fulfilled_callbacks);
           it = neo_list_node_next(it)) {
        neo_js_handle_t hcallback = neo_list_node_get(it);
        neo_js_variable_t callback =
            neo_js_context_create_variable(ctx, hcallback, NULL);
        neo_js_context_create_micro_task(ctx, callback,
                                         neo_js_context_create_undefined(ctx),
                                         1, &value, 0, false);
      }
    }
  }
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t neo_js_reject(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  neo_js_promise_t promise =
      neo_js_context_get_opaque(ctx, self, "[[promise]]");
  if (promise->status == NEO_PROMISE_PENDDING) {
    promise->status = NEO_PROMISE_REJECTED;
    neo_js_variable_t value = NULL;
    if (argc) {
      value = argv[0];
    } else {
      value = neo_js_context_create_undefined(ctx);
    }
    promise->error = neo_js_variable_get_handle(value);
    neo_js_handle_add_parent(promise->error, neo_js_variable_get_handle(self));
    for (neo_list_node_t it =
             neo_list_get_first(promise->on_rejected_callbacks);
         it != neo_list_get_tail(promise->on_rejected_callbacks);
         it = neo_list_node_next(it)) {
      neo_js_handle_t hcallback = neo_list_node_get(it);
      neo_js_variable_t callback =
          neo_js_context_create_variable(ctx, hcallback, NULL);
      neo_js_context_create_micro_task(ctx, callback,
                                       neo_js_context_create_undefined(ctx), 1,
                                       &value, 0, false);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_promise_constructor(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {
  if (neo_js_context_get_call_type(ctx) != NEO_JS_CONSTRUCT_CALL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Promise constructor cannot be invoked without 'new'");
  }
  if (argc < 1 ||
      neo_js_variable_get_type(argv[0])->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        " Promise resolver undefined is not a function");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_promise_t promise = neo_create_js_promise(allocator, ctx);
  neo_js_variable_t resolve =
      neo_js_context_create_cfunction(ctx, "resolve", neo_js_resolve);
  neo_js_callable_set_bind(ctx, resolve, self);
  neo_js_context_set_internal(ctx, self, "[[resolve]]", resolve);
  neo_js_variable_t reject =
      neo_js_context_create_cfunction(ctx, "reject", neo_js_reject);
  neo_js_callable_set_bind(ctx, reject, self);
  neo_js_context_set_internal(ctx, self, "[[reject]]", reject);
  neo_js_context_set_opaque(ctx, self, "[[promise]]", promise);
  neo_js_variable_t resolver = argv[0];
  neo_js_variable_t args[] = {resolve, reject};
  neo_js_variable_t error = neo_js_context_call(
      ctx, resolver, neo_js_context_create_undefined(ctx), 2, args);
  if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
    return error;
  }
  return neo_js_context_create_undefined(ctx);
}
static neo_js_variable_t neo_js_packed_resolve(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  neo_js_variable_t on_fulfilled =
      neo_js_context_load_variable(ctx, "onFulfilled");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, "resolve");
  neo_js_variable_t reject = neo_js_context_load_variable(ctx, "reject");
  if (neo_js_variable_get_type(on_fulfilled)->kind >= NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t result = neo_js_context_call(
        ctx, on_fulfilled, neo_js_context_create_undefined(ctx), argc, argv);
    if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
      return neo_js_context_call(
          ctx, reject, neo_js_context_create_undefined(ctx), 1, &result);
    } else {
      return neo_js_context_call(
          ctx, resolve, neo_js_context_create_undefined(ctx), 1, &result);
    }
  } else {
    return neo_js_context_call(
        ctx, resolve, neo_js_context_create_undefined(ctx), argc, argv);
  }
}

static neo_js_variable_t neo_js_packed_reject(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              uint32_t argc,
                                              neo_js_variable_t *argv) {
  neo_js_variable_t on_rejected =
      neo_js_context_load_variable(ctx, "onRejected");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, "resolve");
  neo_js_variable_t reject = neo_js_context_load_variable(ctx, "reject");
  if (neo_js_variable_get_type(on_rejected)->kind >= NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t result = neo_js_context_call(
        ctx, on_rejected, neo_js_context_create_undefined(ctx), argc, argv);
    if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
      return neo_js_context_call(
          ctx, reject, neo_js_context_create_undefined(ctx), 1, &result);
    } else {
      return neo_js_context_call(
          ctx, resolve, neo_js_context_create_undefined(ctx), 1, &result);
    }
  } else {
    return neo_js_context_call(
        ctx, resolve, neo_js_context_create_undefined(ctx), argc, argv);
  }
}

static neo_js_variable_t neo_js_resolver(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t on_fulfilled =
      neo_js_context_load_variable(ctx, "onFulfilled");
  neo_js_variable_t on_rejected =
      neo_js_context_load_variable(ctx, "onRejected");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t packed_resolve =
      neo_js_context_create_cfunction(ctx, "", neo_js_packed_resolve);
  neo_js_variable_t packed_reject =
      neo_js_context_create_cfunction(ctx, "", neo_js_packed_reject);
  neo_js_callable_set_closure(ctx, packed_resolve, "onFulfilled", on_fulfilled);
  neo_js_callable_set_closure(ctx, packed_resolve, "resolve", resolve);
  neo_js_callable_set_closure(ctx, packed_resolve, "reject", reject);

  neo_js_callable_set_closure(ctx, packed_reject, "onRejected", on_rejected);
  neo_js_callable_set_closure(ctx, packed_reject, "resolve", resolve);
  neo_js_callable_set_closure(ctx, packed_reject, "reject", reject);

  neo_js_promise_t promise =
      neo_js_context_get_opaque(ctx, self, "[[promise]]");
  neo_js_handle_t hpromise = neo_js_variable_get_handle(self);
  if (promise->status == NEO_PROMISE_PENDDING) {
    if (neo_js_variable_get_type(on_fulfilled)->kind >= NEO_JS_TYPE_CALLABLE) {
      neo_js_handle_t hfulfilled = neo_js_variable_get_handle(on_fulfilled);
      neo_list_push(promise->on_fulfilled_callbacks, hfulfilled);
      neo_js_handle_add_parent(hfulfilled, hpromise);
    }
    if (neo_js_variable_get_type(on_rejected)->kind >= NEO_JS_TYPE_CALLABLE) {
      neo_js_handle_t hrejected = neo_js_variable_get_handle(on_rejected);
      neo_list_push(promise->on_rejected_callbacks, hrejected);
      neo_js_handle_add_parent(hrejected, hpromise);
    }
  } else if (promise->status == NEO_PROMISE_FULFILLED) {
    neo_js_variable_t value =
        neo_js_context_create_variable(ctx, promise->value, NULL);
    neo_js_context_create_micro_task(ctx, packed_resolve,
                                     neo_js_context_create_undefined(ctx), 1,
                                     &value, 0, false);
  } else if (promise->status == NEO_PROMISE_REJECTED) {
    neo_js_variable_t value =
        neo_js_context_create_variable(ctx, promise->error, NULL);
    neo_js_context_create_micro_task(ctx, packed_reject,
                                     neo_js_context_create_undefined(ctx), 1,
                                     &value, 0, false);
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_promise_then(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  neo_js_variable_t on_fulfilled = NULL;
  neo_js_variable_t on_rejected = NULL;
  if (argc > 0) {
    on_fulfilled = argv[0];
  }
  if (argc > 1) {
    on_rejected = argv[1];
  }
  if (!on_fulfilled) {
    on_fulfilled = neo_js_context_create_null(ctx);
  }
  if (!on_rejected) {
    on_rejected = neo_js_context_create_null(ctx);
  }
  neo_js_variable_t resolver =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_resolver);
  neo_js_callable_set_closure(ctx, resolver, "onFulfilled", on_fulfilled);
  neo_js_callable_set_closure(ctx, resolver, "onRejected", on_rejected);
  neo_js_callable_set_bind(ctx, resolver, self);
  neo_js_variable_t promise = neo_js_context_get_std(ctx).promise_constructor;
  return neo_js_context_construct(ctx, promise, 1, &resolver);
}

neo_js_variable_t neo_js_promise_catch(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  if (argc > 0) {
    neo_js_variable_t args[] = {neo_js_context_create_undefined(ctx), argv[0]};
    return neo_js_promise_then(ctx, self, 2, args);
  }
  return neo_js_promise_then(ctx, self, 0, NULL);
}
neo_js_variable_t neo_js_promise_finally(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  if (argc > 0) {
    neo_js_variable_t args[] = {argv[0], argv[0]};
    return neo_js_promise_then(ctx, self, 2, args);
  }
  return neo_js_promise_then(ctx, self, 0, NULL);
}

void neo_js_context_init_std_promise(neo_js_context_t ctx) {
  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).promise_constructor,
      neo_js_context_create_string(ctx, "all"),
      neo_js_context_create_cfunction(ctx, "all", neo_js_promise_all), true,
      false, true);

  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).promise_constructor,
                           neo_js_context_create_string(ctx, "allSettled"),
                           neo_js_context_create_cfunction(
                               ctx, "allSettled", neo_js_promise_all_settled),
                           true, false, true);

  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).promise_constructor,
      neo_js_context_create_string(ctx, "any"),
      neo_js_context_create_cfunction(ctx, "any", neo_js_promise_any), true,
      false, true);

  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).promise_constructor,
      neo_js_context_create_string(ctx, "race"),
      neo_js_context_create_cfunction(ctx, "race", neo_js_promise_race), true,
      false, true);

  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).promise_constructor,
      neo_js_context_create_string(ctx, "resolve"),
      neo_js_context_create_cfunction(ctx, "resolve", neo_js_promise_resolve),
      true, false, true);

  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).promise_constructor,
      neo_js_context_create_string(ctx, "reject"),
      neo_js_context_create_cfunction(ctx, "reject", neo_js_promise_reject),
      true, false, true);

  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).promise_constructor,
      neo_js_context_create_string(ctx, "try"),
      neo_js_context_create_cfunction(ctx, "try", neo_js_promise_try), true,
      false, true);

  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).promise_constructor,
      neo_js_context_create_string(ctx, "withResolvers"),
      neo_js_context_create_cfunction(ctx, "withResolvers",
                                      neo_js_promise_with_resolvers),
      true, false, true);

  neo_js_context_def_accessor(
      ctx, neo_js_context_get_std(ctx).promise_constructor,
      neo_js_context_get_string_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor, "species"),
      neo_js_context_create_cfunction(ctx, "[Symbol.species]",
                                      neo_js_promise_species),
      NULL, true, true);

  neo_js_variable_t prototype = neo_js_context_get_string_field(
      ctx, neo_js_context_get_std(ctx).promise_constructor, "prototype");

  neo_js_variable_t to_string_tag = neo_js_context_get_string_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor, "toStringTag");

  neo_js_context_def_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, "Promise"), true,
                           false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "then"),
      neo_js_context_create_cfunction(ctx, "then", neo_js_promise_then), true,
      false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "catch"),
      neo_js_context_create_cfunction(ctx, "catch", neo_js_promise_catch), true,
      false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "finally"),
      neo_js_context_create_cfunction(ctx, "finally", neo_js_promise_finally),
      true, false, true);
}