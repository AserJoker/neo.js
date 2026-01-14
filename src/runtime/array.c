#include "neo.js/runtime/array.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/string.h"
#include "neo.js/engine/array.h"
#include "neo.js/engine/boolean.h"
#include "neo.js/engine/context.h"
#include "neo.js/engine/exception.h"
#include "neo.js/engine/number.h"
#include "neo.js/engine/runtime.h"
#include "neo.js/engine/string.h"
#include "neo.js/engine/value.h"
#include "neo.js/engine/variable.h"
#include "neo.js/runtime/constant.h"
#include "neo.js/runtime/promise.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>


static neo_js_variable_t neo_js_array_get_length(neo_js_context_t ctx,
                                                 neo_js_variable_t array) {
  neo_js_variable_t length = neo_js_variable_get_field(
      array, ctx, neo_js_context_create_cstring(ctx, "length"));
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  length = neo_js_variable_to_number(length, ctx);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  return length;
}

NEO_JS_CFUNCTION(neo_js_array_from) {
  neo_js_variable_t array_like =
      neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t map_fn = neo_js_context_get_undefined(ctx);
  neo_js_variable_t this_arg = neo_js_context_get_undefined(ctx);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  if (argc > 1) {
    map_fn = argv[1];
  }
  if (argc > 2) {
    this_arg = argv[2];
  }
  neo_js_variable_t iterator = neo_js_variable_get_field(
      array_like, ctx, neo_js_context_get_constant(ctx)->symbol_iterator);
  if (iterator->value->type == NEO_JS_TYPE_EXCEPTION) {
    return iterator;
  }
  if (iterator->value->type == NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t generator =
        neo_js_variable_call(iterator, ctx, array_like, 0, NULL);
    if (generator->value->type == NEO_JS_TYPE_EXCEPTION) {
      return generator;
    }
    if (generator->value->type < NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t message = neo_js_context_create_cstring(
          ctx, "Result of the Symbol.iterator method is not an object");
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_variable_t next = neo_js_variable_get_field(
        generator, ctx, neo_js_context_create_cstring(ctx, "next"));
    if (next->value->type == NEO_JS_TYPE_EXCEPTION) {
      return next;
    }
    if (next->value->type != NEO_JS_TYPE_FUNCTION) {
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t message =
          neo_js_context_format(ctx, "%v is not a function", next);
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    size_t idx = 0;
    for (;;) {
      neo_js_variable_t res =
          neo_js_variable_call(next, ctx, generator, 0, NULL);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (res->value->type < NEO_JS_TYPE_OBJECT) {
        neo_js_variable_t type_error =
            neo_js_context_get_constant(ctx)->type_error_class;
        neo_js_variable_t message = neo_js_context_format(
            ctx, "Iterator result %v is not an object", res);
        neo_js_variable_t error =
            neo_js_variable_construct(type_error, ctx, 1, &message);
        return neo_js_context_create_exception(ctx, error);
      }
      neo_js_variable_t done = neo_js_variable_get_field(
          res, ctx, neo_js_context_create_cstring(ctx, "done"));
      if (done->value->type == NEO_JS_TYPE_EXCEPTION) {
        return done;
      }
      done = neo_js_variable_to_boolean(done, ctx);
      if (done->value->type == NEO_JS_TYPE_EXCEPTION) {
        return done;
      }
      if (((neo_js_boolean_t)done->value)->value) {
        break;
      }
      neo_js_variable_t value = neo_js_variable_get_field(
          res, ctx, neo_js_context_create_cstring(ctx, "value"));
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
      if (map_fn->value->type != NEO_JS_TYPE_UNDEFINED) {
        neo_js_variable_t args[] = {value, index};
        value = neo_js_variable_call(map_fn, ctx, this_arg, 2, args);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          return value;
        }
      }
      neo_js_variable_set_field(result, ctx, index, value);
      idx++;
    }
  } else if (array_like->value->type >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t length = neo_js_variable_get_field(
        array_like, ctx, neo_js_context_create_cstring(ctx, "length"));
    if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
      return length;
    }
    length = neo_js_variable_to_number(length, ctx);
    if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
      return length;
    }
    uint32_t len = ((neo_js_number_t)length->value)->value;
    for (uint32_t idx = 0; idx < len; idx++) {
      neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t value =
          neo_js_variable_get_field(array_like, ctx, index);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      if (map_fn->value->type != NEO_JS_TYPE_UNDEFINED) {
        neo_js_variable_t args[] = {value, index};
        value = neo_js_variable_call(map_fn, ctx, this_arg, 2, args);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          return value;
        }
      }
      neo_js_variable_set_field(result, ctx, index, value);
    }
  }
  return result;
}
static NEO_JS_CFUNCTION(neo_js_array_from_async_next_onfulfilled);
static NEO_JS_CFUNCTION(neo_js_array_from_async_next_onrejected);
static NEO_JS_CFUNCTION(neo_js_array_from_async_next_map_onfulfilled) {
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t result = neo_js_context_load(ctx, "result");
  neo_js_variable_t index = neo_js_context_load(ctx, "index");
  neo_js_variable_t map_fn = neo_js_context_load(ctx, "mapFn");
  neo_js_variable_t this_arg = neo_js_context_load(ctx, "thisArg");
  neo_js_variable_t generator = neo_js_context_load(ctx, "generator");
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  uint32_t idx = ((neo_js_number_t)index->value)->value;
  neo_js_variable_set_field(result, ctx, index, value);
  neo_js_variable_t next = neo_js_variable_get_field(
      generator, ctx, neo_js_context_create_cstring(ctx, "next"));
  if (next->value->type == NEO_JS_TYPE_EXCEPTION) {
    return next;
  }
  neo_js_variable_t res = neo_js_variable_call(next, ctx, generator, 0, NULL);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    return res;
  }
  res = neo_js_promise_resolve(
      ctx, neo_js_context_get_constant(ctx)->promise_class, 1, &res);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    return res;
  }
  neo_js_variable_t onfulfilled = neo_js_context_create_cfunction(
      ctx, neo_js_array_from_async_next_onfulfilled, NULL);
  neo_js_variable_set_closure(onfulfilled, ctx, "promise", promise);
  neo_js_variable_set_closure(onfulfilled, ctx, "result", result);
  neo_js_variable_set_closure(onfulfilled, ctx, "mapFn", map_fn);
  neo_js_variable_set_closure(onfulfilled, ctx, "thisArg", this_arg);
  neo_js_variable_set_closure(onfulfilled, ctx, "generator", generator);
  neo_js_variable_set_closure(onfulfilled, ctx, "index",
                              neo_js_context_create_number(ctx, idx + 1));
  neo_js_variable_t onrejected = neo_js_context_create_cfunction(
      ctx, neo_js_array_from_async_next_onrejected, NULL);
  neo_js_variable_set_closure(onrejected, ctx, "promise", promise);
  neo_js_variable_t args[] = {onfulfilled, onrejected};
  neo_js_variable_t then = neo_js_variable_get_field(
      res, ctx, neo_js_context_create_cstring(ctx, "then"));
  neo_js_variable_call(then, ctx, res, 2, args);
  return neo_js_context_get_undefined(ctx);
}
static NEO_JS_CFUNCTION(neo_js_array_from_async_next_value_onfulfilled) {
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t result = neo_js_context_load(ctx, "result");
  neo_js_variable_t index = neo_js_context_load(ctx, "index");
  neo_js_variable_t map_fn = neo_js_context_load(ctx, "mapFn");
  neo_js_variable_t this_arg = neo_js_context_load(ctx, "thisArg");
  neo_js_variable_t generator = neo_js_context_load(ctx, "generator");
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (map_fn->value->type != NEO_JS_TYPE_UNDEFINED) {
    neo_js_variable_t args[] = {value, index};
    value = neo_js_variable_call(map_fn, ctx, this_arg, 2, args);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      neo_js_variable_t error = neo_js_context_create_variable(
          ctx, ((neo_js_exception_t)value->value)->error);
      neo_js_promise_callback_reject(ctx, promise, 1, &error);
      return neo_js_context_get_undefined(ctx);
    }
  }
  value = neo_js_promise_resolve(
      ctx, neo_js_context_get_constant(ctx)->promise_class, 1, &value);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)value->value)->error);
    neo_js_promise_callback_reject(ctx, promise, 1, &error);
    return neo_js_context_get_undefined(ctx);
  }
  neo_js_variable_t onfulfilled = neo_js_context_create_cfunction(
      ctx, neo_js_array_from_async_next_map_onfulfilled, NULL);
  neo_js_variable_set_closure(onfulfilled, ctx, "promise", promise);
  neo_js_variable_set_closure(onfulfilled, ctx, "result", result);
  neo_js_variable_set_closure(onfulfilled, ctx, "mapFn", map_fn);
  neo_js_variable_set_closure(onfulfilled, ctx, "thisArg", this_arg);
  neo_js_variable_set_closure(onfulfilled, ctx, "generator", generator);
  neo_js_variable_set_closure(onfulfilled, ctx, "index", index);
  neo_js_variable_t onrejected = neo_js_context_create_cfunction(
      ctx, neo_js_array_from_async_next_onrejected, NULL);
  neo_js_variable_set_closure(onrejected, ctx, "promise", promise);
  neo_js_variable_t args[] = {onfulfilled, onrejected};
  neo_js_variable_t then = neo_js_variable_get_field(
      value, ctx, neo_js_context_create_cstring(ctx, "then"));
  neo_js_variable_call(then, ctx, value, 2, args);
  return neo_js_context_get_undefined(ctx);
}
static NEO_JS_CFUNCTION(neo_js_array_from_async_next_onfulfilled) {
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t result = neo_js_context_load(ctx, "result");
  neo_js_variable_t index = neo_js_context_load(ctx, "index");
  neo_js_variable_t map_fn = neo_js_context_load(ctx, "mapFn");
  neo_js_variable_t this_arg = neo_js_context_load(ctx, "thisArg");
  neo_js_variable_t generator = neo_js_context_load(ctx, "generator");
  neo_js_variable_t res = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t done = neo_js_variable_get_field(
      res, ctx, neo_js_context_create_cstring(ctx, "done"));
  if (done->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)done->value)->error);
    neo_js_promise_callback_reject(ctx, promise, 1, &error);
    return neo_js_context_get_undefined(ctx);
  }
  done = neo_js_variable_to_boolean(done, ctx);
  if (done->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)done->value)->error);
    neo_js_promise_callback_reject(ctx, promise, 1, &error);
    return neo_js_context_get_undefined(ctx);
  }
  if (((neo_js_boolean_t)done->value)->value) {
    neo_js_promise_callback_resolve(ctx, promise, 1, &result);
    return neo_js_context_get_undefined(ctx);
  }
  neo_js_variable_t value = neo_js_variable_get_field(
      res, ctx, neo_js_context_create_cstring(ctx, "value"));
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)value->value)->error);
    neo_js_promise_callback_reject(ctx, promise, 1, &error);
    return neo_js_context_get_undefined(ctx);
  }
  value = neo_js_promise_resolve(
      ctx, neo_js_context_get_constant(ctx)->promise_class, 1, &value);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)value->value)->error);
    neo_js_promise_callback_reject(ctx, promise, 1, &error);
    return neo_js_context_get_undefined(ctx);
  }
  neo_js_variable_t onfulfilled = neo_js_context_create_cfunction(
      ctx, neo_js_array_from_async_next_value_onfulfilled, NULL);
  neo_js_variable_set_closure(onfulfilled, ctx, "promise", promise);
  neo_js_variable_set_closure(onfulfilled, ctx, "result", result);
  neo_js_variable_set_closure(onfulfilled, ctx, "mapFn", map_fn);
  neo_js_variable_set_closure(onfulfilled, ctx, "thisArg", this_arg);
  neo_js_variable_set_closure(onfulfilled, ctx, "generator", generator);
  neo_js_variable_set_closure(onfulfilled, ctx, "index", index);
  neo_js_variable_t onrejected = neo_js_context_create_cfunction(
      ctx, neo_js_array_from_async_next_onrejected, NULL);
  neo_js_variable_set_closure(onrejected, ctx, "promise", promise);
  neo_js_variable_t args[] = {onfulfilled, onrejected};
  neo_js_variable_t then = neo_js_variable_get_field(
      value, ctx, neo_js_context_create_cstring(ctx, "then"));
  neo_js_variable_call(then, ctx, value, 2, args);
  return neo_js_context_get_undefined(ctx);
}

static NEO_JS_CFUNCTION(neo_js_array_from_async_next_onrejected) {
  neo_js_variable_t promise = neo_js_context_load(ctx, "promise");
  neo_js_variable_t error = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_promise_callback_reject(ctx, promise, 1, &error);
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_array_from_async) {
  neo_js_variable_t promise = neo_js_context_create_promise(ctx);
  neo_js_variable_t array_like =
      neo_js_context_get_argument(ctx, argc, argv, 0);

  neo_js_variable_t map_fn = neo_js_context_get_undefined(ctx);
  neo_js_variable_t this_arg = neo_js_context_get_undefined(ctx);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  if (argc > 1) {
    map_fn = argv[1];
  }
  if (argc > 2) {
    this_arg = argv[2];
  }
  neo_js_variable_t async_iterator = neo_js_variable_get_field(
      array_like, ctx, neo_js_context_get_constant(ctx)->symbol_async_iterator);
  if (async_iterator->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_promise_callback_resolve(ctx, promise, 1, &async_iterator);
    return promise;
  }
  if (async_iterator->value->type >= NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t generator =
        neo_js_variable_call(async_iterator, ctx, array_like, 0, NULL);
    if (generator->value->type == NEO_JS_TYPE_EXCEPTION) {
      return generator;
    }
    neo_js_variable_t next = neo_js_variable_get_field(
        generator, ctx, neo_js_context_create_cstring(ctx, "next"));
    if (next->value->type == NEO_JS_TYPE_EXCEPTION) {
      return next;
    }
    neo_js_variable_t res = neo_js_variable_call(next, ctx, generator, 0, NULL);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    res = neo_js_promise_resolve(
        ctx, neo_js_context_get_constant(ctx)->promise_class, 1, &res);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    neo_js_variable_t onfulfilled = neo_js_context_create_cfunction(
        ctx, neo_js_array_from_async_next_onfulfilled, NULL);
    neo_js_variable_set_closure(onfulfilled, ctx, "promise", promise);
    neo_js_variable_set_closure(onfulfilled, ctx, "result", result);
    neo_js_variable_set_closure(onfulfilled, ctx, "mapFn", map_fn);
    neo_js_variable_set_closure(onfulfilled, ctx, "thisArg", this_arg);
    neo_js_variable_set_closure(onfulfilled, ctx, "generator", generator);
    neo_js_variable_set_closure(onfulfilled, ctx, "index",
                                neo_js_context_create_number(ctx, 0));
    neo_js_variable_t onrejected = neo_js_context_create_cfunction(
        ctx, neo_js_array_from_async_next_onrejected, NULL);
    neo_js_variable_set_closure(onrejected, ctx, "promise", promise);
    neo_js_variable_t args[] = {onfulfilled, onrejected};
    neo_js_variable_t then = neo_js_variable_get_field(
        res, ctx, neo_js_context_create_cstring(ctx, "then"));
    neo_js_variable_call(then, ctx, res, 2, args);
  }
  return promise;
}
NEO_JS_CFUNCTION(neo_js_array_is_array) {
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  return neo_js_variable_has_opaque(value, "is_array")
             ? neo_js_context_get_true(ctx)
             : neo_js_context_get_false(ctx);
}
NEO_JS_CFUNCTION(neo_js_array_of) {
  neo_js_variable_t res = neo_js_context_create_array(ctx);
  neo_js_array_push(ctx, res, argc, argv);
  return res;
}
NEO_JS_CFUNCTION(neo_js_array_constructor) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_array_t array =
      neo_create_js_array(allocator, constant->array_prototype->value);
  self = neo_js_context_create_variable(ctx, neo_js_array_to_value(array));
  if (argc == 1 && argv[0]->value->type == NEO_JS_TYPE_NUMBER) {
    double lenf = ((neo_js_number_t)argv[0]->value)->value;
    uint32_t len = lenf;
    if (len != lenf) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Invalid array length");
      neo_js_variable_t error = neo_js_variable_construct(
          constant->range_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_variable_t length = neo_js_context_create_number(ctx, len);
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, "length");
    neo_js_variable_def_field(self, ctx, key, length, false, false, true);
  } else {
    neo_js_variable_t length = neo_js_context_create_number(ctx, 0);
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, "length");
    neo_js_variable_def_field(self, ctx, key, length, false, false, true);
    for (size_t idx = 0; idx < argc; idx++) {
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t res =
          neo_js_variable_set_field(self, ctx, key, argv[idx]);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    }
  }
  neo_js_variable_set_opaque(self, ctx, "is_array", NULL);
  neo_js_variable_def_field(self, ctx,
                            neo_js_context_create_cstring(ctx, "constructor"),
                            constant->array_class, true, false, true);
  return self;
}

NEO_JS_CFUNCTION(neo_js_array_at) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t index = neo_js_context_get_argument(ctx, argc, argv, 0);
  int64_t idx = 0;
  if (index->value->type != NEO_JS_TYPE_NUMBER) {
    index = neo_js_variable_to_number(index, ctx);
    if (index->value->type == NEO_JS_TYPE_EXCEPTION) {
      return index;
    }
    double val = ((neo_js_number_t)(index->value))->value;
    if (isinf(val)) {
      return neo_js_context_get_undefined(ctx);
    }
    if (!isnan(val)) {
      idx = (int64_t)val;
    }
  }
  neo_js_variable_t length = neo_js_variable_get_field(
      self, ctx, neo_js_context_create_cstring(ctx, "length"));
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  int64_t len = 0;
  if (length->value->type != NEO_JS_TYPE_NUMBER) {
    length = neo_js_variable_to_number(length, ctx);
    if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
      return length;
    }
    double val = ((neo_js_number_t)length->value)->value;
    if (isnan(val) || val < 0) {
      len = 0;
    } else if (val >= ((int64_t)2 << 53)) {
      len = 0;
    }
    if (idx < 0) {
      idx += len;
    }
  }
  if (idx < 0 || idx >= len) {
    return neo_js_context_get_undefined(ctx);
  }
  return neo_js_variable_get_field(self, ctx,
                                   neo_js_context_create_number(ctx, idx));
}

NEO_JS_CFUNCTION(neo_js_array_concat) {
  neo_js_variable_t result = NULL;
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  bool is_array = neo_js_variable_has_opaque(self, "is_array");
  if (is_array) {
    neo_js_variable_t constructor = neo_js_variable_get_field(
        self, ctx, neo_js_context_create_cstring(ctx, "constructor"));
    if (constructor->value->type == NEO_JS_TYPE_EXCEPTION) {
      return constructor;
    }
    constructor =
        neo_js_variable_get_field(constructor, ctx, constant->symbol_species);
    if (constructor->value->type == NEO_JS_TYPE_EXCEPTION) {
      return constructor;
    }
    result = neo_js_variable_construct(constructor, ctx, 0, NULL);
    if (result->value->type == NEO_JS_TYPE_EXCEPTION) {
      return result;
    }
  } else {
    result = neo_js_context_create_array(ctx);
  }
  for (size_t idx = 0; idx <= argc; idx++) {
    neo_js_variable_t item = NULL;
    if (idx == 0) {
      item = self;
    } else {
      item = argv[idx - 1];
    }
    if (item->value->type < NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t err = neo_js_array_push(ctx, result, 1, &item);
      if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
        return err;
      }
      continue;
    }
    bool is_array = neo_js_variable_has_opaque(item, "is_array");
    if (!is_array) {
      neo_js_variable_t is_concat_spreadable = neo_js_variable_get_field(
          item, ctx, constant->symbol_is_concat_spreadable);
      if (is_concat_spreadable->value->type == NEO_JS_TYPE_EXCEPTION) {
        return is_concat_spreadable;
      }
      is_concat_spreadable =
          neo_js_variable_to_boolean(is_concat_spreadable, ctx);
      if (is_concat_spreadable->value->type == NEO_JS_TYPE_EXCEPTION) {
        return is_concat_spreadable;
      }
      if (!((neo_js_boolean_t)is_concat_spreadable->value)->value) {
        neo_js_variable_t err = neo_js_array_push(ctx, result, 1, &self);
        if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
          return err;
        }
        continue;
      }
    }
    neo_js_variable_t length = neo_js_variable_get_field(
        item, ctx, neo_js_context_create_cstring(ctx, "length"));
    if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
      return length;
    }
    length = neo_js_variable_to_number(length, ctx);
    if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
      return length;
    }
    double lenf = ((neo_js_number_t)length->value)->value;
    for (int64_t idx = 0; idx < lenf; idx++) {
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      key = neo_js_variable_to_string(key, ctx);
      if (neo_js_variable_get_property(item, ctx, key)) {
        neo_js_variable_t val = neo_js_variable_get_field(item, ctx, key);
        if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
          return val;
        }
        neo_js_variable_t err = neo_js_array_push(ctx, result, 1, &val);
        if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
          return err;
        }
      } else {
        neo_js_variable_t length = neo_js_variable_get_field(
            result, ctx, neo_js_context_create_cstring(ctx, "length"));
        ((neo_js_number_t)length->value)->value += 1;
      }
    }
  }
  return result;
}

NEO_JS_CFUNCTION(neo_js_array_copy_within) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  neo_js_variable_t length = neo_js_variable_get_field(
      self, ctx, neo_js_context_create_cstring(ctx, "length"));
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  length = neo_js_variable_to_number(length, ctx);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  if (isnan(len) || len <= 0) {
    return self;
  }
  int64_t index = 0;
  int64_t start = 0;
  double end = len;
  if (argc > 0) {
    neo_js_variable_t idx = argv[0];
    idx = neo_js_variable_to_number(idx, ctx);
    if (idx->value->type == NEO_JS_TYPE_EXCEPTION) {
      return idx;
    }
    double val = ((neo_js_number_t)idx->value)->value;
    if (isnan(val)) {
      val = 0;
    }
    if (val < 0) {
      val += len;
    }
    if (val >= len || val < 0) {
      return self;
    }
    index = (int64_t)val;
  }
  if (argc > 1) {
    neo_js_variable_t st = argv[1];
    st = neo_js_variable_to_number(st, ctx);
    if (st->value->type == NEO_JS_TYPE_EXCEPTION) {
      return st;
    }
    double val = ((neo_js_number_t)st->value)->value;
    if (isnan(val)) {
      val = 0;
    }
    if (val < 0) {
      val += len;
    }
    if (val >= len || val < 0) {
      return self;
    }
    start = (int64_t)val;
  }
  if (argc > 2) {
    neo_js_variable_t ed = argv[2];
    ed = neo_js_variable_to_number(ed, ctx);
    if (ed->value->type == NEO_JS_TYPE_EXCEPTION) {
      return ed;
    }
    double val = ((neo_js_number_t)ed->value)->value;
    if (isnan(val)) {
      val = 0;
    }
    if (val < 0) {
      val += len;
    }
    if (val >= len || val < 0) {
      return self;
    }
    end = (int64_t)val;
  }
  if (end - start + index >= len) {
    end = start + len - index;
  }
  if (start >= end) {
    return self;
  }
  for (int64_t idx = start; idx < end; idx++) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    key = neo_js_variable_to_string(key, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t error = neo_js_variable_set_field(
          self, ctx, neo_js_context_create_number(ctx, idx - start + index),
          value);
      if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
        return error;
      }
    } else {
      neo_js_variable_t error = neo_js_variable_del_field(
          self, ctx, neo_js_context_create_number(ctx, idx - start + index));
      if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
        return error;
      }
    }
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_array_to_string) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_context_create_cstring(ctx, "length");
  length = neo_js_variable_get_field(self, ctx, length);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  if (length->value->type != NEO_JS_TYPE_NUMBER) {
    length = neo_js_variable_to_number(length, ctx);
  }
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double lenf = ((neo_js_number_t)length->value)->value;
  if (lenf < 0 || isnan(lenf)) {
    lenf = 0;
  }
  int64_t len = lenf;
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  size_t max = 16;
  uint16_t *string =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * max, NULL);
  *string = 0;
  uint16_t *dst = string;
  for (size_t idx = 0; idx < len; idx++) {
    if (idx != 0) {
      uint16_t part[] = {',', 0};
      dst = neo_string16_concat(allocator, dst, &max, part);
    }
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    index = neo_js_variable_to_string(index, ctx);
    if (index->value->type == NEO_JS_TYPE_EXCEPTION) {
      return index;
    }
    if (neo_js_variable_get_property(self, ctx, index)) {
      neo_js_variable_t item = neo_js_variable_get_field(self, ctx, index);
      if (item->value->type != NEO_JS_TYPE_STRING) {
        item = neo_js_variable_to_string(item, ctx);
        if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
          return item;
        }
      }
      dst = neo_string16_concat(allocator, dst, &max,
                                ((neo_js_string_t)item->value)->value);
    }
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, string);
  neo_allocator_free(allocator, string);
  return result;
}
NEO_JS_CFUNCTION(neo_js_array_values) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t iterator =
      neo_js_context_create_object(ctx, constant->array_iterator_prototype);
  neo_js_variable_set_internal(iterator, ctx, "array", self);
  neo_js_variable_t index = neo_js_context_create_number(ctx, 0);
  neo_js_variable_set_internal(iterator, ctx, "index", index);
  return iterator;
}
NEO_JS_CFUNCTION(neo_js_array_push) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "length");
  neo_js_variable_t length = neo_js_variable_get_field(self, ctx, key);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  if (length->value->type != NEO_JS_TYPE_NUMBER) {
    length = neo_js_variable_to_number(length, ctx);
  }
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double lenf = ((neo_js_number_t)length->value)->value;
  if (isnan(lenf) || lenf < 0) {
    lenf = 0;
  }
  int64_t len = lenf;
  length = neo_js_context_create_number(ctx, len);
  neo_js_number_t num = (neo_js_number_t)length->value;
  for (size_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, num->value);
    num->value++;
    if (num->value >= ((int64_t)2 << 52) - 1) {
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t message = neo_js_context_format(
          ctx,
          "Pushing 1 elements on an array-like of length %v "
          "is disallowed, as the total surpasses 2**53-1",
          length);
      neo_js_variable_t error = neo_js_variable_construct(
          constant->type_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_variable_t res =
        neo_js_variable_set_field(self, ctx, index, argv[idx]);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
  }
  return length;
}
NEO_JS_CFUNCTION(neo_js_array_get_symbol_species) { return self; }
void neo_initialize_js_array(neo_js_context_t ctx) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_array_t array = neo_create_js_array(allocator, constant->null->value);
  constant->array_prototype =
      neo_js_context_create_variable(ctx, neo_js_array_to_value(array));
  constant->array_class =
      neo_js_context_create_cfunction(ctx, neo_js_array_constructor, "Array");
  neo_js_variable_set_prototype_of(constant->array_class, ctx,
                                   constant->function_prototype);
  neo_js_variable_def_field(constant->array_class, ctx, constant->key_prototype,
                            constant->array_prototype, true, false, true);

  NEO_JS_DEF_METHOD(ctx, constant->array_class, "from", neo_js_array_from);
  NEO_JS_DEF_METHOD(ctx, constant->array_class, "fromAsync",
                    neo_js_array_from_async);
  NEO_JS_DEF_METHOD(ctx, constant->array_class, "of", neo_js_array_of);
  NEO_JS_DEF_METHOD(ctx, constant->array_class, "isArray",
                    neo_js_array_is_array);

  neo_js_variable_t values =
      neo_js_context_create_cfunction(ctx, neo_js_array_values, "values");

  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "at", neo_js_array_at);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "concat",
                    neo_js_array_concat);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "copyWithin",
                    neo_js_array_copy_within);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "toString",
                    neo_js_array_to_string);
  neo_js_variable_def_field(constant->array_prototype, ctx,
                            neo_js_context_create_cstring(ctx, "values"),
                            values, true, false, true);
  neo_js_variable_def_field(constant->array_prototype, ctx,
                            constant->symbol_iterator, values, true, false,
                            true);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "push", neo_js_array_push);
  neo_js_variable_def_accessor(constant->array_class, ctx,
                               constant->symbol_species,
                               neo_js_context_create_cfunction(
                                   ctx, neo_js_array_get_symbol_species, NULL),
                               NULL, true, false);
}