#include "neojs/runtime/array.h"
#include "neojs/core/allocator.h"
#include "neojs/core/string.h"
#include "neojs/engine/array.h"
#include "neojs/engine/boolean.h"
#include "neojs/engine/context.h"
#include "neojs/engine/exception.h"
#include "neojs/engine/number.h"
#include "neojs/engine/runtime.h"
#include "neojs/engine/string.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
#include "neojs/runtime/promise.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static neo_js_variable_t neo_js_array_get_length(neo_js_context_t ctx,
                                                 neo_js_variable_t array) {
  neo_js_variable_t length = neo_js_variable_get_field(
      array, ctx, neo_js_context_create_string(ctx, u"length"));
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  length = neo_js_variable_to_integer(length, ctx);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double val = ((neo_js_number_t)length->value)->value;
  if (val < 0) {
    val = 0;
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
      neo_js_variable_t message = neo_js_context_create_string(
          ctx, u"Result of the Symbol.iterator method is not an object");
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_variable_t next = neo_js_variable_get_field(
        generator, ctx, neo_js_context_create_string(ctx, u"next"));
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
          res, ctx, neo_js_context_create_string(ctx, u"done"));
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
          res, ctx, neo_js_context_create_string(ctx, u"value"));
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
    neo_js_variable_t length = neo_js_array_get_length(ctx, array_like);
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
      generator, ctx, neo_js_context_create_string(ctx, u"next"));
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
      res, ctx, neo_js_context_create_string(ctx, u"then"));
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
      value, ctx, neo_js_context_create_string(ctx, u"then"));
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
      res, ctx, neo_js_context_create_string(ctx, u"done"));
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
      res, ctx, neo_js_context_create_string(ctx, u"value"));
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
      value, ctx, neo_js_context_create_string(ctx, u"then"));
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
        generator, ctx, neo_js_context_create_string(ctx, u"next"));
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
        res, ctx, neo_js_context_create_string(ctx, u"then"));
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
    neo_js_variable_t key = neo_js_context_create_string(ctx, u"length");
    neo_js_variable_def_field(self, ctx, key, length, false, false, true);
  } else {
    neo_js_variable_t length = neo_js_context_create_number(ctx, 0);
    neo_js_variable_t key = neo_js_context_create_string(ctx, u"length");
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
                            neo_js_context_create_string(ctx, u"constructor"),
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
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
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
        self, ctx, neo_js_context_create_string(ctx, u"constructor"));
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
    neo_js_variable_t length = neo_js_array_get_length(ctx, item);
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
            result, ctx, neo_js_context_create_string(ctx, u"length"));
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
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  double index = 0;
  double start = 0;
  double end = len;
  if (argc > 0) {
    neo_js_variable_t idx = argv[0];
    idx = neo_js_variable_to_integer(idx, ctx);
    if (idx->value->type == NEO_JS_TYPE_EXCEPTION) {
      return idx;
    }
    index = ((neo_js_number_t)idx->value)->value;
    if (index < 0) {
      index += len;
    }
  }
  if (argc > 1) {
    neo_js_variable_t st = argv[1];
    st = neo_js_variable_to_integer(st, ctx);
    if (st->value->type == NEO_JS_TYPE_EXCEPTION) {
      return st;
    }
    start = ((neo_js_number_t)st->value)->value;
    if (start < 0) {
      start += len;
    }
  }
  if (argc > 2) {
    neo_js_variable_t ed = argv[2];
    ed = neo_js_variable_to_integer(ed, ctx);
    if (ed->value->type == NEO_JS_TYPE_EXCEPTION) {
      return ed;
    }
    end = ((neo_js_number_t)ed->value)->value;
    if (end < 0) {
      end += len;
    }
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

NEO_JS_CFUNCTION(neo_js_array_entries) {
  neo_js_variable_t iterator = neo_js_array_values(ctx, self, argc, argv);
  neo_js_variable_set_opaque(iterator, ctx, "is_entries", NULL);
  return iterator;
}
NEO_JS_CFUNCTION(neo_js_array_every) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length)->value;
  neo_js_variable_t cb = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_undefined(ctx);
  for (int64_t idx = 0; idx < len; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    index = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, index)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, index);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t args[] = {value, neo_js_context_create_number(ctx, idx),
                                  self};
      neo_js_variable_t res = neo_js_variable_call(cb, ctx, this_arg, 3, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      res = neo_js_variable_to_boolean(res, ctx);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (!((neo_js_boolean_t)res->value)->value) {
        return res;
      }
    }
  }
  return neo_js_context_get_true(ctx);
}
NEO_JS_CFUNCTION(neo_js_array_fill) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  if (len == 0) {
    return self;
  }
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  double start = 0;
  double end = len;
  if (argc > 1) {
    neo_js_variable_t s = argv[1];
    s = neo_js_variable_to_integer(s, ctx);
    if (s->value->type == NEO_JS_TYPE_EXCEPTION) {
      return s;
    }
    start = ((neo_js_number_t)s->value)->value;
  }
  if (start < 0) {
    start += len;
    if (start < 0) {
      return self;
    }
  }
  if (start >= len) {
    return self;
  }
  if (argc > 2) {
    neo_js_variable_t s = argv[1];
    s = neo_js_variable_to_integer(s, ctx);
    if (s->value->type == NEO_JS_TYPE_EXCEPTION) {
      return s;
    }
    end = ((neo_js_number_t)s->value)->value;
  }
  if (end < 0) {
    end += len;
    if (end < 0) {
      return self;
    }
  }
  if (end == 0 || end <= start) {
    return self;
  }
  for (double idx = start; idx != end; idx += 1) {
    neo_js_variable_t res = neo_js_variable_set_field(
        self, ctx, neo_js_context_create_number(ctx, idx), value);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
  }
  return self;
}

NEO_JS_CFUNCTION(neo_js_array_filter) {
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
        self, ctx, neo_js_context_create_string(ctx, u"constructor"));
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
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  for (double idx = 0; idx < len; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    neo_js_variable_t args[] = {value, key, self};
    neo_js_variable_t res =
        neo_js_variable_call(callback, ctx, this_arg, 3, args);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    res = neo_js_variable_to_boolean(res, ctx);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    if (((neo_js_boolean_t)res->value)->value) {
      neo_js_variable_t err = neo_js_array_push(ctx, result, 1, &value);
      if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
        return err;
      }
    }
  }
  return result;
}

NEO_JS_CFUNCTION(neo_js_array_find) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  for (double idx = 0; idx < len; idx += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t args[] = {value, index, self};
      neo_js_variable_t res =
          neo_js_variable_call(callback, ctx, this_arg, 3, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      res = neo_js_variable_to_boolean(res, ctx);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (((neo_js_boolean_t)res->value)->value) {
        return value;
      }
    }
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_array_find_index) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  for (double idx = 0; idx < len; idx += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t args[] = {value, index, self};
      neo_js_variable_t res =
          neo_js_variable_call(callback, ctx, this_arg, 3, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      res = neo_js_variable_to_boolean(res, ctx);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (((neo_js_boolean_t)res->value)->value) {
        return index;
      }
    }
  }
  return neo_js_context_create_number(ctx, -1);
}
NEO_JS_CFUNCTION(neo_js_array_find_last) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  for (double idx = len - 1; idx >= 0; idx -= 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t args[] = {value, index, self};
      neo_js_variable_t res =
          neo_js_variable_call(callback, ctx, this_arg, 3, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      res = neo_js_variable_to_boolean(res, ctx);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (((neo_js_boolean_t)res->value)->value) {
        return value;
      }
    }
  }
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_array_find_last_index) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  for (double idx = len - 1; idx >= 0; idx -= 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t args[] = {value, index, self};
      neo_js_variable_t res =
          neo_js_variable_call(callback, ctx, this_arg, 3, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      res = neo_js_variable_to_boolean(res, ctx);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (((neo_js_boolean_t)res->value)->value) {
        return index;
      }
    }
  }
  return neo_js_context_create_number(ctx, -1);
}
static neo_js_variable_t neo_js_array_flat_item(neo_js_context_t ctx,
                                                neo_js_variable_t result,
                                                neo_js_variable_t item,
                                                double depth) {
  if (neo_js_variable_has_opaque(item, "is_array") || depth <= 0) {
    if (depth > 0) {
      neo_js_variable_t length = neo_js_array_get_length(ctx, item);
      if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
        return length;
      }
      double len = ((neo_js_number_t)length->value)->value;
      for (double idx = 0; idx < len; idx += 1) {
        neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
        neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
        if (!neo_js_variable_get_property(item, ctx, key)) {
          neo_js_variable_t length = neo_js_array_get_length(ctx, result);
          if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
            return length;
          }
          ((neo_js_number_t)length->value)->value += 1;
          neo_js_variable_t err = neo_js_variable_set_field(
              result, ctx, neo_js_context_create_string(ctx, u"length"),
              length);
          if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
            return err;
          }
        } else {
          neo_js_variable_t val = neo_js_variable_get_field(item, ctx, key);
          if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
            return val;
          }
          neo_js_variable_t err =
              neo_js_array_flat_item(ctx, result, val, depth - 1);
          if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
            return err;
          }
        }
      }
    }
    return result;
  } else {
    return neo_js_array_push(ctx, result, 1, &item);
  }
}
NEO_JS_CFUNCTION(neo_js_array_flat) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t result = NULL;
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  bool is_array = neo_js_variable_has_opaque(self, "is_array");
  if (is_array) {
    neo_js_variable_t constructor = neo_js_variable_get_field(
        self, ctx, neo_js_context_create_string(ctx, u"constructor"));
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
  double depth = 1;
  if (argc > 0) {
    neo_js_variable_t val = argv[0];
    val = neo_js_variable_to_number(val, ctx);
    if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
      return val;
    }
    depth = ((neo_js_number_t)val->value)->value;
    if (isnan(depth) || depth < 0) {
      depth = 0;
    }
    if (!isinf(depth)) {
      depth = (int64_t)depth;
    }
  }
  for (double idx = 0; idx < len; idx += 1) {
    neo_js_variable_t value = neo_js_variable_get_field(
        self, ctx, neo_js_context_create_number(ctx, idx));
    neo_js_variable_t res = neo_js_array_flat_item(ctx, result, value, depth);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
  }
  return result;
}

NEO_JS_CFUNCTION(neo_js_array_flat_map) {
  neo_js_variable_t res = neo_js_array_map(ctx, self, argc, argv);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    return res;
  }
  return neo_js_array_flat(ctx, self, 0, NULL);
}
NEO_JS_CFUNCTION(neo_js_array_for_each) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  for (double idx = 0; idx < len; idx += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t args[] = {value, index, self};
      neo_js_variable_t res =
          neo_js_variable_call(callback, ctx, this_arg, 3, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    }
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_array_includes) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t val = neo_js_context_get_argument(ctx, argc, argv, 0);
  for (double idx = 0; idx < len; idx += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t value = neo_js_variable_get_field(self, ctx, index);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    if (value->value->type == val->value->type) {
      neo_js_variable_t res = neo_js_variable_seq(value, ctx, val);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (((neo_js_boolean_t)res->value)->value) {
        return neo_js_context_get_true(ctx);
      }
      if (res->value->type == NEO_JS_TYPE_NUMBER) {
        if (isnan(((neo_js_number_t)value->value)->value) &&
            isnan(((neo_js_number_t)val->value)->value)) {
          return neo_js_context_get_true(ctx);
        }
      }
    }
  }
  return neo_js_context_get_false(ctx);
}
NEO_JS_CFUNCTION(neo_js_array_index_of) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t val = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t start_index =
      neo_js_context_get_argument(ctx, argc, argv, 1);
  start_index = neo_js_variable_to_integer(start_index, ctx);
  if (start_index->value->type == NEO_JS_TYPE_EXCEPTION) {
    return start_index;
  }
  double start = ((neo_js_number_t)start_index->value)->value;
  if (start < 0) {
    start += len;
  }
  if (start < 0) {
    return neo_js_context_create_number(ctx, -1);
  }
  for (double idx = start; idx < len; idx += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t value = neo_js_variable_get_field(self, ctx, index);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    if (value->value->type == val->value->type) {
      neo_js_variable_t res = neo_js_variable_seq(value, ctx, val);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (((neo_js_boolean_t)res->value)->value) {
        return index;
      }
      if (res->value->type == NEO_JS_TYPE_NUMBER) {
        if (isnan(((neo_js_number_t)value->value)->value) &&
            isnan(((neo_js_number_t)val->value)->value)) {
          return index;
        }
      }
    }
  }
  return neo_js_context_create_number(ctx, -1);
}
NEO_JS_CFUNCTION(neo_js_array_join) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  const uint16_t *separator = NULL;
  if (argc > 0) {
    neo_js_variable_t sep = neo_js_context_get_argument(ctx, argc, argv, 0);
    sep = neo_js_variable_to_string(sep, ctx);
    if (sep->value->type == NEO_JS_TYPE_EXCEPTION) {
      return sep;
    }
    separator = ((neo_js_string_t)sep->value)->value;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *str = neo_allocator_alloc(allocator, sizeof(uint16_t) * 16, NULL);
  str[0] = 0;
  size_t strlength = 16;
  for (double idx = 0; idx < len; idx += 1) {
    if (separator && idx != 0) {
      str = neo_string16_concat(allocator, str, &strlength, separator);
    }
    neo_js_variable_t item = neo_js_variable_get_field(
        self, ctx, neo_js_context_create_number(ctx, idx));
    if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
      neo_allocator_free(allocator, str);
      return item;
    }
    if (item->value->type != NEO_JS_TYPE_UNDEFINED) {
      item = neo_js_variable_to_string(item, ctx);
      if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, str);
        return item;
      }
      str = neo_string16_concat(allocator, str, &strlength,
                                ((neo_js_string_t)item->value)->value);
    }
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, str);
  neo_allocator_free(allocator, str);
  return result;
}
NEO_JS_CFUNCTION(neo_js_array_keys) {
  neo_js_variable_t keys = neo_js_context_create_array(ctx);
  return keys;
}
NEO_JS_CFUNCTION(neo_js_array_last_index_of) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  double start = len - 1;
  neo_js_variable_t val = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (argc > 1) {
    neo_js_variable_t start_index =
        neo_js_context_get_argument(ctx, argc, argv, 1);
    start_index = neo_js_variable_to_integer(start_index, ctx);
    if (start_index->value->type == NEO_JS_TYPE_EXCEPTION) {
      return start_index;
    }
    start = ((neo_js_number_t)start_index->value)->value;
    if (start < 0) {
      start += len;
    }
    if (start < 0) {
      return neo_js_context_create_number(ctx, -1);
    }
    if (start >= len) {
      start = len - 1;
    }
  }
  for (double idx = start; idx >= 0; idx -= 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t value = neo_js_variable_get_field(self, ctx, index);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    if (value->value->type == val->value->type) {
      neo_js_variable_t res = neo_js_variable_seq(value, ctx, val);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (((neo_js_boolean_t)res->value)->value) {
        return index;
      }
      if (res->value->type == NEO_JS_TYPE_NUMBER) {
        if (isnan(((neo_js_number_t)value->value)->value) &&
            isnan(((neo_js_number_t)val->value)->value)) {
          return index;
        }
      }
    }
  }
  return neo_js_context_create_number(ctx, -1);
}
NEO_JS_CFUNCTION(neo_js_array_map) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
    if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
      return self;
    }
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  for (double idx = 0; idx < len; idx += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value;
      }
      neo_js_variable_t args[] = {value, index, self};
      neo_js_variable_t res =
          neo_js_variable_call(callback, ctx, this_arg, 3, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      res = neo_js_variable_set_field(result, ctx, index, res);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    } else {
      neo_js_variable_t length = neo_js_variable_get_field(
          result, ctx, neo_js_context_create_string(ctx, u"length"));
      ((neo_js_number_t)length->value)->value += 1;
    }
  }
  return result;
}

NEO_JS_CFUNCTION(neo_js_array_push) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
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
  neo_js_variable_t error = neo_js_variable_set_field(
      self, ctx, neo_js_context_create_string(ctx, u"length"), length);
  if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
    return error;
  }
  return length;
}
NEO_JS_CFUNCTION(neo_js_array_pop) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  if (len > 0) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, len - 1);
    neo_js_variable_t res = neo_js_variable_get_field(self, ctx, key);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    neo_js_variable_t error = neo_js_variable_del_field(self, ctx, key);
    if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
      return error;
    }
    error = neo_js_variable_set_field(
        self, ctx, neo_js_context_create_string(ctx, u"length"),
        neo_js_context_create_number(ctx, len - 1));
    if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
      return error;
    }
    return res;
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_array_reduce) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t cb = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t init = NULL;
  if (argc) {
    init = argv[0];
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  double start = 0;
  if (!init) {
    if (len == 0) {
      neo_js_variable_t message = neo_js_context_create_string(
          ctx, u"Reduce of empty array with no initial value");
      neo_js_variable_t error = neo_js_variable_construct(
          neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    init = neo_js_variable_get_field(
        self, ctx,
        neo_js_variable_get_field(self, ctx,
                                  neo_js_context_create_number(ctx, 0)));
    if (init->value->type == NEO_JS_TYPE_EXCEPTION) {
      return init;
    }
    start = 1;
  }
  neo_js_variable_t res = init;
  while (start < len) {
    neo_js_variable_t idx = neo_js_context_create_number(ctx, start);
    neo_js_variable_t item = neo_js_variable_get_field(self, ctx, idx);
    if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
      return item;
    }
    neo_js_variable_t args[] = {res, item, idx, self};
    res = neo_js_variable_call(cb, ctx, neo_js_context_get_undefined(ctx), 4,
                               args);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    start += 1;
  }
  return res;
}
NEO_JS_CFUNCTION(neo_js_array_reduce_right) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t cb = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t init = NULL;
  if (argc) {
    init = argv[0];
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  double start = len - 1;
  if (!init) {
    if (len == 0) {
      neo_js_variable_t message = neo_js_context_create_string(
          ctx, u"Reduce of empty array with no initial value");
      neo_js_variable_t error = neo_js_variable_construct(
          neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    init = neo_js_variable_get_field(
        self, ctx,
        neo_js_variable_get_field(self, ctx,
                                  neo_js_context_create_number(ctx, len - 1)));
    if (init->value->type == NEO_JS_TYPE_EXCEPTION) {
      return init;
    }
    start = len - 2;
  }
  neo_js_variable_t res = init;
  while (start >= 0) {
    neo_js_variable_t idx = neo_js_context_create_number(ctx, start);
    neo_js_variable_t item = neo_js_variable_get_field(self, ctx, idx);
    if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
      return item;
    }
    neo_js_variable_t args[] = {res, item, idx, self};
    res = neo_js_variable_call(cb, ctx, neo_js_context_get_undefined(ctx), 4,
                               args);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    start -= 1;
  }
  return res;
}
NEO_JS_CFUNCTION(neo_js_array_reverse) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  double mid = (int64_t)(len / 2);
  for (double idx = 0; idx < mid; idx += 1) {
    neo_js_variable_t index1 = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t index2 = neo_js_context_create_number(ctx, len - 1 - idx);
    neo_js_variable_t key1 = neo_js_variable_to_string(index1, ctx);
    neo_js_variable_t key2 = neo_js_variable_to_string(index2, ctx);
    neo_js_variable_t value1 = NULL;
    neo_js_variable_t value2 = NULL;
    if (neo_js_variable_get_property(self, ctx, key1)) {
      value1 = neo_js_variable_get_field(self, ctx, key1);
      if (value1->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value1;
      }
    }
    if (neo_js_variable_get_property(self, ctx, key2)) {
      value2 = neo_js_variable_get_field(self, ctx, key2);
      if (value2->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value2;
      }
    }
    if (value1) {
      neo_js_variable_t res =
          neo_js_variable_set_field(self, ctx, key2, value1);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    } else {
      neo_js_variable_t res = neo_js_variable_del_field(self, ctx, key2);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    }
    if (value2) {
      neo_js_variable_t res =
          neo_js_variable_set_field(self, ctx, key1, value2);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    } else {
      neo_js_variable_t res = neo_js_variable_del_field(self, ctx, key1);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    }
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_array_shift) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  if (len > 0) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, 0);
    neo_js_variable_t res = neo_js_variable_get_field(self, ctx, key);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
    for (double idx = 0; idx < len - 1; idx += 1) {
      neo_js_variable_t index1 = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t index2 = neo_js_context_create_number(ctx, idx + 1);
      neo_js_variable_t key1 = neo_js_variable_to_string(index1, ctx);
      neo_js_variable_t key2 = neo_js_variable_to_string(index2, ctx);
      if (neo_js_variable_get_property(self, ctx, key2)) {
        neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key2);
        if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
          return value;
        }
        neo_js_variable_t res =
            neo_js_variable_set_field(self, ctx, key1, value);
        if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
          return res;
        }
      } else {
        neo_js_variable_t error = neo_js_variable_del_field(self, ctx, key1);
        if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
          return error;
        }
      }
    }
    neo_js_variable_t error = neo_js_variable_set_field(
        self, ctx, neo_js_context_create_string(ctx, u"length"),
        neo_js_context_create_number(ctx, len - 1));
    if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
      return error;
    }
    return res;
  }
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_array_slice) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t result = NULL;
  bool is_array = neo_js_variable_has_opaque(self, "is_array");
  if (is_array) {
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t constructor = neo_js_variable_get_field(
        self, ctx, neo_js_context_create_string(ctx, u"constructor"));
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
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  double start = 0;
  double end = len;
  if (argc > 0) {
    neo_js_variable_t arg = argv[0];
    arg = neo_js_variable_to_integer(arg, ctx);
    if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
      return arg;
    }
    start = ((neo_js_number_t)arg->value)->value;
  }
  if (argc > 1) {
    neo_js_variable_t arg = argv[0];
    arg = neo_js_variable_to_integer(arg, ctx);
    if (arg->value->type == NEO_JS_TYPE_EXCEPTION) {
      return arg;
    }
    end = ((neo_js_number_t)arg->value)->value;
  }
  if (start < 0) {
    start += len;
  }
  if (start < 0 || start >= len) {
    return result;
  }
  if (end < 0) {
    end += len;
  }
  if (end < 0 || end <= start) {
    return result;
  }
  if (end > len) {
    end = len;
  }
  for (double idx = start; idx < end; idx += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t item = neo_js_variable_get_field(self, ctx, key);
      if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
        return item;
      }
      neo_js_variable_t error =
          neo_js_variable_set_field(result, ctx, key, item);
      if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
        return error;
      }
    }
  }
  neo_js_variable_t error = neo_js_variable_set_field(
      result, ctx, neo_js_context_create_string(ctx, u"length"),
      neo_js_context_create_number(ctx, end - start));
  if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
    return error;
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_array_some) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t result = NULL;
  bool is_array = neo_js_variable_has_opaque(self, "is_array");
  if (is_array) {
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t constructor = neo_js_variable_get_field(
        self, ctx, neo_js_context_create_string(ctx, u"constructor"));
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
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  for (double idx = 0; idx < len; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t item = neo_js_variable_get_field(self, ctx, key);
      if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
        return item;
      }
      neo_js_variable_t args[] = {item, index, self};
      neo_js_variable_t res =
          neo_js_variable_call(callback, ctx, this_arg, 3, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      res = neo_js_variable_to_boolean(res, ctx);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      if (((neo_js_boolean_t)res)->value) {
        return neo_js_context_get_true(ctx);
      }
    }
  }
  return neo_js_context_get_false(ctx);
}

neo_js_variable_t neo_js_array_qsort(neo_js_context_t ctx,
                                     neo_js_variable_t *array, int64_t start,
                                     int64_t end, neo_js_variable_t compare,
                                     neo_js_variable_t this_arg) {
  if (end - start <= 1) {
    return neo_js_context_get_undefined(ctx);
  }
  int64_t flag = start;
  for (int64_t idx = start + 1; idx < end; idx++) {
    bool flag = false;
    if (array[flag] == NULL) {
      flag = false;
    } else if (array[idx] == NULL) {
      flag = true;
    } else if (array[idx]->value->type == NEO_JS_TYPE_UNDEFINED) {
      if (array[flag]->value->type != NEO_JS_TYPE_UNDEFINED) {
        flag = true;
      }
    } else if (compare->value->type != NEO_JS_TYPE_UNDEFINED) {
      neo_js_variable_t args[] = {array[idx], array[flag]};
      neo_js_variable_t res =
          neo_js_variable_call(compare, ctx, this_arg, 2, args);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      res = neo_js_variable_to_number(res, ctx);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
      flag = ((neo_js_number_t)res->value)->value > 0;
    }
    if (flag) {
      neo_js_variable_t tmp = array[idx];
      array[idx] = array[flag];
      array[flag] = tmp;
      flag = idx;
    }
  }
  neo_js_variable_t error =
      neo_js_array_qsort(ctx, array, start, flag, compare, this_arg);
  if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
    return error;
  }
  error = neo_js_array_qsort(ctx, array, flag + 1, end, compare, this_arg);
  if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
    return error;
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_array_sort) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double lenf = ((neo_js_number_t)length->value)->value;
  int64_t len = (int64_t)lenf;
  if (lenf != len) {
    neo_js_variable_t message =
        neo_js_context_create_string(ctx, u"Invalid array length");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->range_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t compare = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t *array =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * len, NULL);
  for (int64_t idx = 0; idx < len; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, array);
        return value;
      }
      array[idx] = value;
    } else {
      array[idx] = NULL;
    }
  }
  neo_js_variable_t error =
      neo_js_array_qsort(ctx, array, 0, len, compare, this_arg);
  if (error->value->type != NEO_JS_TYPE_EXCEPTION) {
    neo_allocator_free(allocator, array);
    return error;
  }
  for (int64_t idx = 0; idx < len; idx++) {
    if (array[idx]) {
      neo_js_variable_t error = neo_js_variable_set_field(
          self, ctx, neo_js_context_create_number(ctx, idx), array[idx]);
      if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
        return error;
      }
    } else {
      neo_js_variable_t error = neo_js_variable_del_field(
          self, ctx, neo_js_context_create_number(ctx, idx));
      if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
        return error;
      }
    }
  }
  neo_allocator_free(allocator, array);
  return self;
}
NEO_JS_CFUNCTION(neo_js_array_splice) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  double len = ((neo_js_number_t)length->value)->value;
  double start = 0;
  if (argc > 0) {
    neo_js_variable_t val = neo_js_variable_to_integer(argv[0], ctx);
    if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
      return val;
    }
    start = ((neo_js_number_t)val->value)->value;
    if (start < 0) {
      start += len;
    }
  }
  double dcount = len - start;
  if (argc > 1) {
    neo_js_variable_t val = neo_js_variable_to_integer(argv[1], ctx);
    if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
      return val;
    }
    dcount = ((neo_js_number_t)val->value)->value;
    if (dcount < 0) {
      start = 0;
    }
  }
  double delta = 0;
  if (argc > 2) {
    delta = argc - 2 - delta;
  }
  for (double idx = 0; idx < dcount; idx += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx + start);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t item = neo_js_variable_get_field(self, ctx, key);
      if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
        return item;
      }
      neo_js_variable_t err = neo_js_variable_set_field(result, ctx, key, item);
      if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
        return err;
      }
    } else {
      neo_js_variable_t len = neo_js_variable_get_field(
          result, ctx, neo_js_context_create_string(ctx, u"length"));
      ((neo_js_number_t)len->value)->value += 1;
    }
  }
  if (delta > 0) {
    for (double idx = start + dcount; idx < len; idx += 1) {
      neo_js_variable_t index1 = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t index2 = neo_js_context_create_number(ctx, idx - delta);
      neo_js_variable_t key = neo_js_variable_to_number(index1, ctx);
      if (neo_js_variable_get_property(self, ctx, key)) {
        neo_js_variable_t item = neo_js_variable_get_field(self, ctx, key);
        if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
          return item;
        }
        neo_js_variable_t err =
            neo_js_variable_set_field(self, ctx, index2, item);
        if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
          return err;
        }
      } else {
        neo_js_variable_t err = neo_js_variable_del_field(self, ctx, key);
        if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
          return err;
        }
      }
    }
  } else {
    for (double idx = len - 1; idx >= start + dcount; idx -= 1) {
      neo_js_variable_t index1 = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t index2 = neo_js_context_create_number(ctx, idx - delta);
      neo_js_variable_t key = neo_js_variable_to_number(index1, ctx);
      if (neo_js_variable_get_property(self, ctx, key)) {
        neo_js_variable_t item = neo_js_variable_get_field(self, ctx, key);
        if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
          return item;
        }
        neo_js_variable_t err =
            neo_js_variable_set_field(self, ctx, index2, item);
        if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
          return err;
        }
      } else {
        neo_js_variable_t err = neo_js_variable_del_field(self, ctx, key);
        if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
          return err;
        }
      }
    }
  }
  for (uint32_t idx = 0; idx < argc - 2; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx + start);
    neo_js_variable_t err =
        neo_js_variable_set_field(self, ctx, index, argv[idx]);
    if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
      return err;
    }
  }
  length = neo_js_context_create_number(ctx, len + delta);
  neo_js_variable_t err = neo_js_variable_set_field(
      self, ctx, neo_js_context_create_string(ctx, u"length"), length);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  return result;
}

NEO_JS_CFUNCTION(neo_js_array_to_locale_string) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  uint16_t separator[] = {',', 0};
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *str = neo_allocator_alloc(allocator, sizeof(uint16_t) * 16, NULL);
  str[0] = 0;
  size_t strlength = 16;
  for (double idx = 0; idx < len; idx += 1) {
    if (idx != 0) {
      str = neo_string16_concat(allocator, str, &strlength, separator);
    }
    neo_js_variable_t item = neo_js_variable_get_field(
        self, ctx, neo_js_context_create_number(ctx, idx));
    if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
      neo_allocator_free(allocator, str);
      return item;
    }
    if (item->value->type != NEO_JS_TYPE_UNDEFINED) {
      if (item->value->type != NEO_JS_TYPE_NULL) {
        item = neo_js_variable_to_object(item, ctx);
        if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
          return item;
        }
        neo_js_variable_t to_locale_string = neo_js_variable_get_field(
            item, ctx, neo_js_context_create_string(ctx, u"toLocaleString"));
        if (to_locale_string->value->type == NEO_JS_TYPE_EXCEPTION) {
          return to_locale_string;
        }
        item = neo_js_variable_call(to_locale_string, ctx, item, argc, argv);
        if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
          return item;
        }
      }
      item = neo_js_variable_to_string(item, ctx);
      if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, str);
        return item;
      }
      str = neo_string16_concat(allocator, str, &strlength,
                                ((neo_js_string_t)item->value)->value);
    }
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, str);
  neo_allocator_free(allocator, str);
  return result;
}

NEO_JS_CFUNCTION(neo_js_array_to_reversed) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  double mid = (int64_t)(len / 2);
  neo_js_variable_t reversed = neo_js_context_create_array(ctx);
  for (double idx = 0; idx < mid; idx += 1) {
    neo_js_variable_t index1 = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t index2 = neo_js_context_create_number(ctx, len - 1 - idx);
    neo_js_variable_t key1 = neo_js_variable_to_string(index1, ctx);
    neo_js_variable_t key2 = neo_js_variable_to_string(index2, ctx);
    neo_js_variable_t value1 = NULL;
    neo_js_variable_t value2 = NULL;
    if (neo_js_variable_get_property(self, ctx, key1)) {
      value1 = neo_js_variable_get_field(self, ctx, key1);
      if (value1->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value1;
      }
    }
    if (neo_js_variable_get_property(self, ctx, key2)) {
      value2 = neo_js_variable_get_field(self, ctx, key2);
      if (value2->value->type == NEO_JS_TYPE_EXCEPTION) {
        return value2;
      }
    }
    if (value1) {
      neo_js_variable_t res =
          neo_js_variable_set_field(reversed, ctx, key2, value1);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    }
    if (value2) {
      neo_js_variable_t res =
          neo_js_variable_set_field(reversed, ctx, key1, value2);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    }
  }
  neo_js_variable_set_field(
      reversed, ctx, neo_js_context_create_string(ctx, u"length"), length);
  return reversed;
}

NEO_JS_CFUNCTION(neo_js_array_to_sorted) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double lenf = ((neo_js_number_t)length->value)->value;
  int64_t len = (int64_t)lenf;
  if (lenf != len) {
    neo_js_variable_t message =
        neo_js_context_create_string(ctx, u"Invalid array length");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->range_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t sorted = neo_js_context_create_array(ctx);
  neo_js_variable_t compare = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t this_arg = neo_js_context_get_argument(ctx, argc, argv, 1);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t *array =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * len, NULL);
  for (int64_t idx = 0; idx < len; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t value = neo_js_variable_get_field(self, ctx, key);
      if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
        neo_allocator_free(allocator, array);
        return value;
      }
      array[idx] = value;
    } else {
      array[idx] = NULL;
    }
  }
  neo_js_variable_t error =
      neo_js_array_qsort(ctx, array, 0, len, compare, this_arg);
  if (error->value->type != NEO_JS_TYPE_EXCEPTION) {
    neo_allocator_free(allocator, array);
    return error;
  }
  for (int64_t idx = 0; idx < len; idx++) {
    if (array[idx]) {
      neo_js_variable_t error = neo_js_variable_set_field(
          sorted, ctx, neo_js_context_create_number(ctx, idx), array[idx]);
      if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
        return error;
      }
    }
  }
  neo_allocator_free(allocator, array);
  neo_js_variable_set_field(
      sorted, ctx, neo_js_context_create_string(ctx, u"length"), length);
  return sorted;
}

NEO_JS_CFUNCTION(neo_js_array_to_spliced) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  double start = 0;
  if (argc > 0) {
    neo_js_variable_t val = neo_js_variable_to_integer(argv[0], ctx);
    if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
      return val;
    }
    start = ((neo_js_number_t)val->value)->value;
    if (start < 0) {
      start += len;
    }
  }
  double dcount = len - start;
  if (argc > 1) {
    neo_js_variable_t val = neo_js_variable_to_integer(argv[1], ctx);
    if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
      return val;
    }
    dcount = ((neo_js_number_t)val->value)->value;
    if (dcount < 0) {
      start = 0;
    }
  }
  neo_js_variable_t spliced = neo_js_context_create_array(ctx);
  for (int64_t i = 0; i < start; i += 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, i);
    neo_js_variable_t item = neo_js_variable_get_field(self, ctx, index);
    if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
      return item;
    }
    neo_js_variable_t res = neo_js_array_push(ctx, spliced, 1, &item);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
  }
  for (int64_t i = 0; i < argc - 2; i++) {
    neo_js_variable_t item = argv[i + 2];
    neo_js_variable_t res = neo_js_array_push(ctx, spliced, 1, &item);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
  }
  for (int64_t i = 0; i < len - start - dcount; i++) {
    neo_js_variable_t index =
        neo_js_context_create_number(ctx, i + start + dcount);
    neo_js_variable_t item = neo_js_variable_get_field(self, ctx, index);
    if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
      return item;
    }
    neo_js_variable_t res = neo_js_array_push(ctx, spliced, 1, &item);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
  }
  return spliced;
}
NEO_JS_CFUNCTION(neo_js_array_to_string) {
  neo_js_variable_t sp = neo_js_context_create_string(ctx, u",");
  return neo_js_array_join(ctx, self, 1, &sp);
}
NEO_JS_CFUNCTION(neo_js_array_unshift) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t res = neo_js_variable_set_field(
      self, ctx, neo_js_context_create_string(ctx, u"length"),
      neo_js_context_create_number(ctx, len + 1));
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    return res;
  }
  for (double idx = len - 1; idx >= 0; idx -= 1) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key = neo_js_variable_to_string(index, ctx);
    neo_js_variable_t target = neo_js_context_create_number(ctx, idx + 1);
    if (neo_js_variable_get_property(self, ctx, key)) {
      neo_js_variable_t item = neo_js_variable_get_field(self, ctx, key);
      if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
        return item;
      }
      neo_js_variable_t res =
          neo_js_variable_set_field(self, ctx, target, item);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    } else {
      neo_js_variable_t err = neo_js_variable_del_field(self, ctx, target);
      if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
        return err;
      }
    }
  }
  return self;
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
NEO_JS_CFUNCTION(neo_js_array_with) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_array_get_length(ctx, self);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_js_variable_t err = neo_js_variable_set_field(
      result, ctx, neo_js_context_create_string(ctx, u"length"), length);
  if (err->value->type == NEO_JS_TYPE_EXCEPTION) {
    return err;
  }
  neo_js_variable_t index = neo_js_context_get_argument(ctx, argc, argv, 0);
  index = neo_js_variable_to_integer(index, ctx);
  if (index->value->type == NEO_JS_TYPE_EXCEPTION) {
    return index;
  }
  double idx = ((neo_js_number_t)index->value)->value;
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  for (double i = 0; i < len; i += 1) {
    neo_js_variable_t item = value;
    if (i != idx) {
      item = neo_js_variable_get_field(self, ctx,
                                       neo_js_context_create_number(ctx, i));
      if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
        return item;
      }
    }
    neo_js_variable_t error = neo_js_variable_set_field(
        self, ctx, neo_js_context_create_number(ctx, i), item);
    if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
      return error;
    }
  }
  if (idx >= len) {
    neo_js_variable_t error = neo_js_variable_set_field(
        self, ctx, neo_js_context_create_number(ctx, idx), value);
    if (error->value->type == NEO_JS_TYPE_EXCEPTION) {
      return error;
    }
  }
  return result;
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
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "entries",
                    neo_js_array_entries);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "every",
                    neo_js_array_every);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "fill", neo_js_array_fill);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "filter",
                    neo_js_array_filter);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "find", neo_js_array_find);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "findIndex",
                    neo_js_array_find_index);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "findLast",
                    neo_js_array_find_last);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "findLastIndex",
                    neo_js_array_find_last_index);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "flat", neo_js_array_flat);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "flatMap",
                    neo_js_array_flat_map);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "forEach",
                    neo_js_array_for_each);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "includes",
                    neo_js_array_includes);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "indexOf",
                    neo_js_array_index_of);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "keys", neo_js_array_keys);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "join", neo_js_array_join);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "lastIndexOf",
                    neo_js_array_last_index_of);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "map", neo_js_array_map);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "pop", neo_js_array_pop);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "push", neo_js_array_push);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "reduce",
                    neo_js_array_reduce);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "reduceRight",
                    neo_js_array_reduce_right);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "reduceRight",
                    neo_js_array_reverse);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "shift",
                    neo_js_array_shift);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "slice",
                    neo_js_array_slice);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "some", neo_js_array_some);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "sort", neo_js_array_sort);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "splice",
                    neo_js_array_splice);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "toLocaleString",
                    neo_js_array_to_locale_string);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "toReversed",
                    neo_js_array_to_reversed);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "toSorted",
                    neo_js_array_to_sorted);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "toString",
                    neo_js_array_to_string);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "unshift",
                    neo_js_array_unshift);
  neo_js_variable_def_field(constant->array_prototype, ctx,
                            neo_js_context_create_string(ctx, u"values"),
                            values, true, false, true);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "with", neo_js_array_with);
  neo_js_variable_def_field(constant->array_prototype, ctx,
                            constant->symbol_iterator, values, true, false,
                            true);
  neo_js_variable_def_accessor(constant->array_class, ctx,
                               constant->symbol_species,
                               neo_js_context_create_cfunction(
                                   ctx, neo_js_array_get_symbol_species, NULL),
                               NULL, true, false);
}