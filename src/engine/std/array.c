#include "engine/std/array.h"
#include "core/allocator.h"
#include "core/list.h"
#include "engine/basetype/array.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/interrupt.h"
#include "engine/basetype/number.h"
#include "engine/basetype/object.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>

neo_js_variable_t neo_js_array_from(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  if (argc < 1) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "undefined is not iterable (cannot read property "
        "Symbol(Symbol.iterator))");
  }
  neo_js_variable_t array_like = argv[0];
  neo_js_variable_t map_fn = NULL;
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    map_fn = argv[1];
  }
  if (argc > 2) {
    this_arg = argv[2];
  }
  if (map_fn && !this_arg) {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "iterator"), NULL);

  neo_js_variable_t constructor = self;
  if (neo_js_variable_get_type(constructor)->kind < NEO_JS_TYPE_CALLABLE) {
    constructor = neo_js_context_get_std(ctx).array_constructor;
  }
  if (neo_js_context_has_field(ctx, array_like, iterator)) {
    iterator = neo_js_context_get_field(ctx, array_like, iterator, NULL);
    if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "%Array%.from requires that the property of the first argument, "
          "items[Symbol.iterator], when exists, be a function");
    }
    neo_js_variable_t generator =
        neo_js_context_call(ctx, iterator, array_like, 0, NULL);
    if (neo_js_variable_get_type(generator)->kind == NEO_JS_TYPE_ERROR) {
      return generator;
    }
    if (neo_js_variable_get_type(generator)->kind < NEO_JS_TYPE_OBJECT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Result of the Symbol.iterator method is not an object");
    }

    neo_js_variable_t next = neo_js_context_get_field(
        ctx, generator, neo_js_context_create_string(ctx, "next"), NULL);
    if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0, "#Object.next is not a function");
    }
    neo_js_variable_t res = neo_js_context_call(ctx, next, generator, 0, NULL);
    if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_ERROR) {
      return res;
    }
    neo_js_variable_t done = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "done"), NULL);

    neo_js_variable_t value = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "value"), NULL);

    done = neo_js_context_to_boolean(ctx, done);
    if (neo_js_variable_get_type(done)->kind == NEO_JS_TYPE_ERROR) {
      return done;
    }
    neo_js_variable_t array =
        neo_js_context_construct(ctx, constructor, 0, NULL);
    size_t idx = 0;
    while (!neo_js_variable_to_boolean(done)->boolean) {
      neo_js_variable_t error = neo_js_context_set_field(
          ctx, array, neo_js_context_create_number(ctx, idx), value, NULL);
      if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
        return error;
      }
      res = neo_js_context_call(ctx, next, generator, 0, NULL);
      if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_ERROR) {
        return res;
      }
      done = neo_js_context_get_field(
          ctx, res, neo_js_context_create_string(ctx, "done"), NULL);
      value = neo_js_context_get_field(
          ctx, res, neo_js_context_create_string(ctx, "value"), NULL);
      done = neo_js_context_to_boolean(ctx, done);
      if (neo_js_variable_get_type(done)->kind == NEO_JS_TYPE_ERROR) {
        return done;
      }
    }
    return array;
  } else if (neo_js_context_has_field(
                 ctx, array_like,
                 neo_js_context_create_string(ctx, "length"))) {
    neo_js_variable_t vlength = neo_js_context_get_field(
        ctx, array_like, neo_js_context_create_string(ctx, "length"), NULL);
    vlength = neo_js_context_to_integer(ctx, vlength);
    if (neo_js_variable_get_type(vlength)->kind == NEO_JS_TYPE_ERROR) {
      return vlength;
    }
    neo_js_number_t num = neo_js_variable_to_number(vlength);
    if (num->number < 0 || num->number > NEO_MAX_INTEGER) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE, 0,
                                                "Invalid array length");
    }
    neo_js_variable_t array =
        neo_js_context_construct(ctx, constructor, 1, &vlength);
    for (uint32_t idx = 0; idx < num->number; idx++) {
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t item =
          neo_js_context_get_field(ctx, array_like, key, NULL);
      if (map_fn) {
        neo_js_variable_t args[] = {item, key};
        item = neo_js_context_call(ctx, map_fn, this_arg, 2, args);
        if (neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_ERROR) {
          return item;
        }
      }
      neo_js_variable_t error =
          neo_js_context_set_field(ctx, array, key, item, NULL);
      if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
        return error;
      }
    }
    neo_js_variable_t error = neo_js_context_set_field(
        ctx, array, neo_js_context_create_string(ctx, "length"), vlength, NULL);
    if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
      return error;
    }
    return array;
  } else {
    return neo_js_context_create_array(ctx);
  }
}

neo_js_variable_t neo_js_array_from_async(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv,
                                          neo_js_variable_t value,
                                          size_t stage) {
  switch (stage) {
  case 0: {
    if (argc < 1) {
      break;
    }
    neo_js_variable_t array_like = argv[0];
    neo_js_variable_t async_iterator = neo_js_context_get_field(
        ctx, neo_js_context_get_std(ctx).symbol_constructor,
        neo_js_context_create_string(ctx, "asyncIterator"), NULL);
    if (!neo_js_context_has_field(ctx, array_like, async_iterator)) {
      break;
    }
    async_iterator =
        neo_js_context_get_field(ctx, array_like, async_iterator, NULL);
    NEO_JS_TRY_AND_THROW(async_iterator);
    if (neo_js_variable_get_type(async_iterator)->kind < NEO_JS_TYPE_CALLABLE) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "%Array%.from requires that the property of the first argument, "
          "items[Symbol.asyncIterator], when exists, be a function");
    }
    neo_js_variable_t generator =
        neo_js_context_call(ctx, async_iterator, array_like, 0, NULL);
    NEO_JS_TRY_AND_THROW(generator);
    if (neo_js_variable_get_type(generator)->kind < NEO_JS_TYPE_OBJECT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Result of the Symbol.asyncIterator method is not an object");
    }
    neo_js_context_def_variable(ctx, generator, "generator");
    neo_js_variable_t result = neo_js_context_create_array(ctx);
    neo_js_context_def_variable(ctx, result, "result");

    return neo_js_context_create_interrupt(
        ctx, neo_js_context_create_undefined(ctx), 1, NEO_JS_INTERRUPT_AWAIT);
  }
  case 1: {
    neo_js_variable_t generator =
        neo_js_context_load_variable(ctx, "generator");
    NEO_JS_TRY_AND_THROW(generator);
    neo_js_variable_t next = neo_js_context_get_field(
        ctx, generator, neo_js_context_create_string(ctx, "next"), NULL);
    if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0, "#Object.next is not a function");
    }
    neo_js_variable_t res = neo_js_context_call(ctx, next, generator, 0, NULL);
    return neo_js_context_create_interrupt(ctx, res, 2, NEO_JS_INTERRUPT_AWAIT);
  }
  case 2: {
    NEO_JS_TRY_AND_THROW(value);
    neo_js_variable_t vdone = neo_js_context_get_field(
        ctx, value, neo_js_context_create_string(ctx, "done"), NULL);
    NEO_JS_TRY_AND_THROW(vdone);
    neo_js_variable_t val = neo_js_context_get_field(
        ctx, value, neo_js_context_create_string(ctx, "value"), NULL);
    NEO_JS_TRY_AND_THROW(val);
    neo_js_variable_t result = neo_js_context_load_variable(ctx, "result");
    NEO_JS_TRY_AND_THROW(result);
    bool done = neo_js_variable_to_boolean(vdone)->boolean;
    if (done) {
      return result;
    }
    neo_js_variable_t v_length = neo_js_context_get_field(
        ctx, result, neo_js_context_create_string(ctx, "length"), NULL);
    NEO_JS_TRY_AND_THROW(
        neo_js_context_set_field(ctx, result, v_length, val, NULL));
    return neo_js_context_create_interrupt(
        ctx, neo_js_context_create_undefined(ctx), 1, NEO_JS_INTERRUPT_AWAIT);
  }
  default:
    break;
  }
  return neo_js_array_from(ctx, self, argc, argv);
}

neo_js_variable_t neo_js_array_is_array(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  if (argc < 1) {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_variable_t array = neo_js_context_get_std(ctx).array_constructor;
  neo_js_variable_t item = argv[0];
  if (neo_js_variable_get_type(item)->kind >= NEO_JS_TYPE_OBJECT) {
    neo_js_object_t obj = neo_js_variable_to_object(item);
    if (obj->constructor && neo_js_chunk_get_value(obj->constructor) ==
                                neo_js_variable_get_value(array)) {
      return neo_js_context_create_boolean(ctx, true);
    }
  }
  return neo_js_context_create_boolean(ctx, false);
}

neo_js_variable_t neo_js_array_of(neo_js_context_t ctx, neo_js_variable_t self,
                                  uint32_t argc, neo_js_variable_t *argv) {
  if (argc == 1) {
    neo_js_variable_t array = neo_js_context_create_array(ctx);
    neo_js_variable_t error = neo_js_context_set_field(
        ctx, array, neo_js_context_create_number(ctx, 0), argv[0], NULL);
    if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
      return error;
    }
    return array;
  }
  return neo_js_array_constructor(ctx, self, argc, argv);
}

neo_js_variable_t neo_js_array_species(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  return self;
}

neo_js_variable_t neo_js_array_constructor(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  neo_js_variable_t constructor = neo_js_object_get_constructor(ctx, self);
  neo_js_variable_t prototype = neo_js_object_get_prototype(ctx, self);
  neo_js_chunk_t hconstructor = neo_js_variable_get_chunk(constructor);
  neo_js_chunk_t hprototype = neo_js_variable_get_chunk(prototype);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_array_t arr = neo_create_js_array(allocator);
  neo_js_chunk_t harr = neo_create_js_chunk(allocator, &arr->object.value);
  arr->object.constructor = hconstructor;
  arr->object.prototype = hprototype;
  neo_js_chunk_add_parent(hconstructor, harr);
  neo_js_chunk_add_parent(hprototype, harr);
  self = neo_js_context_create_variable(ctx, harr, NULL);
  neo_js_context_def_field(
      ctx, self, neo_js_context_create_string(ctx, "length"),
      neo_js_context_create_number(ctx, 0), false, false, true);

  if (argc == 1 &&
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NUMBER) {
    uint32_t length = 0;
    neo_js_number_t number = neo_js_variable_to_number(argv[0]);
    if (number->number < 0 || number->number > 0xffffffff) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE, 0,
                                                "Invalid array length");
    } else if (!isnan(number->number)) {
      length = number->number;
    }
    neo_js_variable_t error = neo_js_context_set_field(
        ctx, self, neo_js_context_create_string(ctx, "length"),
        neo_js_context_create_number(ctx, length), NULL);
    if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
      return error;
    }
    return self;
  }
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t error = neo_js_context_set_field(
        ctx, self, neo_js_context_create_number(ctx, idx), argv[idx], NULL);
    if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
      return error;
    }
  }
  neo_js_variable_t len = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  return self;
}

neo_js_variable_t neo_js_array_at(neo_js_context_t ctx, neo_js_variable_t self,
                                  uint32_t argc, neo_js_variable_t *argv) {
  int64_t idx = 0;
  if (argc > 0) {
    neo_js_variable_t index = argv[0];
    index = neo_js_context_to_number(ctx, index);
    neo_js_number_t num = neo_js_variable_to_number(index);
    if (!isnan(num->number)) {
      idx = num->number;
    }
  }
  if (idx < 0) {
    neo_js_variable_t length = neo_js_context_get_field(
        ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
    length = neo_js_context_to_number(ctx, length);
    neo_js_number_t num = neo_js_variable_to_number(length);
    if (isnan(num->number) || num->number < 0 ||
        num->number > NEO_MAX_INTEGER) {
      return neo_js_context_create_undefined(ctx);
    }
    idx += num->number;
  }
  if (idx < 0) {
    return neo_js_context_create_undefined(ctx);
  }
  return neo_js_context_get_field(ctx, self,
                                  neo_js_context_create_number(ctx, idx), NULL);
}

neo_js_variable_t neo_js_array_concat(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Array.prototype.concat called on null or undefined");
  }
  neo_js_variable_t res = neo_js_context_create_array(ctx);

  neo_js_variable_t is_concat_spreadable = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "is_concat_spreadable"), NULL);
  size_t index = 0;
  for (size_t count = 0; count < argc + 1; count++) {
    neo_js_variable_t item = NULL;
    if (count == 0) {
      item = self;
    } else {
      item = argv[count - 1];
    }
    if (neo_js_variable_get_type(item)->kind < NEO_JS_TYPE_OBJECT &&
        count == 0) {
      item = neo_js_context_to_object(ctx, item);
      if (neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_ERROR) {
        return item;
      }
    }
    if (neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_ARRAY) {
      neo_js_variable_t vlength = neo_js_context_get_field(
          ctx, item, neo_js_context_create_string(ctx, "length"), NULL);
      neo_js_number_t num = neo_js_variable_to_number(vlength);
      for (uint32_t idx = 0; idx < num->number; idx++) {
        neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
        if (neo_js_context_has_field(ctx, item, key)) {
          neo_js_variable_t value =
              neo_js_context_get_field(ctx, item, key, NULL);
          neo_js_variable_t error = neo_js_context_set_field(
              ctx, res, neo_js_context_create_number(ctx, index), value, NULL);
          if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
            return error;
          }
        }
        index++;
      }
    } else if (neo_js_variable_get_type(item)->kind < NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t error = neo_js_context_set_field(
          ctx, res, neo_js_context_create_number(ctx, index), item, NULL);
      if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
        return error;
      }
      index++;
    } else {
      neo_js_variable_t flag =
          neo_js_context_get_field(ctx, item, is_concat_spreadable, NULL);
      flag = neo_js_context_to_boolean(ctx, flag);
      if (neo_js_variable_to_boolean(flag)->boolean) {
        neo_js_variable_t vlength = neo_js_context_get_field(
            ctx, item, neo_js_context_create_string(ctx, "length"), NULL);
        vlength = neo_js_context_to_number(ctx, vlength);
        neo_js_number_t num = neo_js_variable_to_number(vlength);
        uint32_t length = 0;
        if (isnan(num->number) || isinf(num->number) || num->number < 0) {
          length = 0;
        } else if (num->number > NEO_MAX_INTEGER) {
          return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE, 0,
                                                    "Invalid array length");
        } else {
          length = num->number;
        }
        for (uint32_t idx = 0; idx < length; idx++) {
          neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
          if (neo_js_context_has_field(ctx, item, key)) {
            neo_js_variable_t value =
                neo_js_context_get_field(ctx, item, key, NULL);
            neo_js_variable_t error = neo_js_context_set_field(
                ctx, res, neo_js_context_create_number(ctx, index), value,
                NULL);
            if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
              return error;
            }
          }
          index++;
        }
      } else {
        neo_js_variable_t error = neo_js_context_set_field(
            ctx, res, neo_js_context_create_number(ctx, index), item, NULL);
        if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
          return error;
        }
        index++;
      }
    }
  }
  neo_js_variable_t error = neo_js_context_set_field(
      ctx, res, neo_js_context_create_string(ctx, "length"),
      neo_js_context_create_number(ctx, index), NULL);
  if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
    return error;
  }
  return res;
}
neo_js_variable_t neo_js_array_copy_within(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {

  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT) {
    self = neo_js_context_to_object(ctx, self);
    if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_ERROR) {
      return self;
    }
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double target = 0;
  double start = 0;
  double end = 0;
  if (argc > 0) {
    neo_js_variable_t v_target = neo_js_context_to_integer(ctx, argv[0]);
    target = neo_js_variable_to_number(v_target)->number;
  }
  if (argc > 1) {
    neo_js_variable_t v_start = neo_js_context_to_integer(ctx, argv[1]);
    start = neo_js_variable_to_number(v_start)->number;
  }
  if (argc > 2) {
    neo_js_variable_t v_end = neo_js_context_to_integer(ctx, argv[2]);
    end = neo_js_variable_to_number(v_end)->number;
  } else {
    end = neo_js_variable_to_number(vlength)->number;
  }
  if (target < 0) {
    target += neo_js_variable_to_number(vlength)->number;
  }
  if (target < 0) {
    target = 0;
  }
  if (start < 0) {
    start += neo_js_variable_to_number(vlength)->number;
  }
  if (start < 0) {
    start = 0;
  }
  if (end >= neo_js_variable_to_number(vlength)->number) {
    end = neo_js_variable_to_number(vlength)->number;
  }
  if (end < 0) {
    end += neo_js_variable_to_number(vlength)->number;
  }
  if (end < 0) {
    end = 0;
  }
  for (double index = 0; index < end - start; index += 1) {
    double src = index + start;
    double dst = index + target;
    if (dst >= neo_js_variable_to_number(vlength)->number) {
      break;
    }
    neo_js_variable_t src_key = neo_js_context_create_number(ctx, src);
    neo_js_variable_t dst_key = neo_js_context_create_number(ctx, dst);
    if (neo_js_context_has_field(ctx, self, src_key)) {
      neo_js_variable_t error = neo_js_context_set_field(
          ctx, self, neo_js_context_create_number(ctx, dst),
          neo_js_context_get_field(ctx, self, src_key, NULL), NULL);
      if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
        return error;
      }
    } else {
      neo_js_variable_t error = neo_js_context_del_field(ctx, self, dst_key);
      if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
        return error;
      }
    }
  }
  return self;
}

neo_js_variable_t neo_js_array_entries(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t item = neo_js_context_create_array(ctx);
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t value = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(value);
    neo_js_variable_t err = neo_js_context_set_field(
        ctx, item, neo_js_context_create_number(ctx, 0), key, NULL);
    NEO_JS_TRY_AND_THROW(err);
    err = neo_js_context_set_field(
        ctx, item, neo_js_context_create_number(ctx, 1), value, NULL);
    NEO_JS_TRY_AND_THROW(err);
    err = neo_js_context_set_field(ctx, array, key, item, NULL);
    NEO_JS_TRY_AND_THROW(err);
  }
  neo_js_variable_t values = neo_js_context_get_field(
      ctx, array, neo_js_context_create_string(ctx, "values"), NULL);
  NEO_JS_TRY_AND_THROW(values);
  return neo_js_context_call(ctx, values, array, 0, NULL);
}

neo_js_variable_t neo_js_array_every(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  }
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  for (double idx = 0; idx < length; idx++) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    neo_js_variable_t res = neo_js_context_call(ctx, fn, this_arg, 3, args);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (!neo_js_variable_to_boolean(res)->boolean) {
      return res;
    }
  }
  return neo_js_context_create_boolean(ctx, true);
}

neo_js_variable_t neo_js_array_fill(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_OBJECT) {
    self = neo_js_context_to_object(ctx, self);
    if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_ERROR) {
      return self;
    }
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  double start = 0;
  double end = 0;
  neo_js_variable_t value = NULL;
  if (argc > 0) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  if (argc > 1) {
    neo_js_variable_t v_start = neo_js_context_to_integer(ctx, argv[1]);
    start = neo_js_variable_to_number(v_start)->number;
  }
  if (argc > 2) {
    neo_js_variable_t v_end = neo_js_context_to_integer(ctx, argv[2]);
    end = neo_js_variable_to_number(v_end)->number;
  } else {
    end = neo_js_variable_to_number(vlength)->number;
  }
  if (start < 0) {
    start += neo_js_variable_to_number(vlength)->number;
  }
  if (start < 0) {
    start = 0;
  }
  if (end >= neo_js_variable_to_number(vlength)->number) {
    end = neo_js_variable_to_number(vlength)->number;
  }
  if (end < 0) {
    end += neo_js_variable_to_number(vlength)->number;
  }
  if (end < 0) {
    end = 0;
  }
  for (double idx = 0; idx < end - start; idx += 1) {
    double dst = idx + start;
    if (dst >= length) {
      break;
    }
    neo_js_variable_t key = neo_js_context_create_number(ctx, dst);
    NEO_JS_TRY_AND_THROW(neo_js_context_set_field(ctx, self, key, value, NULL));
  }
  return self;
}

neo_js_variable_t neo_js_array_filter(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  double index = 0;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    neo_js_variable_t res = neo_js_context_call(ctx, fn, this_arg, 3, args);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
          ctx, array, neo_js_context_create_number(ctx, index), item, NULL));
      index += 1;
    }
  }
  return array;
}

neo_js_variable_t neo_js_array_find(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;

  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    neo_js_variable_t res = neo_js_context_call(ctx, fn, this_arg, 3, args);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      return item;
    }
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_array_find_index(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;

  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    neo_js_variable_t res = neo_js_context_call(ctx, fn, this_arg, 3, args);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      return key;
    }
  }
  return neo_js_context_create_number(ctx, -1);
}

neo_js_variable_t neo_js_array_find_last(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;

  for (double idx = length - 1; idx >= 0; idx -= 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    neo_js_variable_t res = neo_js_context_call(ctx, fn, this_arg, 3, args);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      return item;
    }
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_array_find_last_index(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;

  for (double idx = length - 1; idx >= 0; idx -= 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    neo_js_variable_t res = neo_js_context_call(ctx, fn, this_arg, 3, args);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      return key;
    }
  }
  return neo_js_context_create_number(ctx, -1);
}

static neo_js_variable_t neo_js_array_flat_depth(neo_js_context_t ctx,
                                                 neo_js_variable_t array,
                                                 double *index,
                                                 neo_js_variable_t self,
                                                 double depth, double current) {
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    if (neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_ARRAY &&
        current < depth) {
      neo_js_variable_t error =
          neo_js_array_flat_depth(ctx, array, index, item, depth, current + 1);
      if (error) {
        return error;
      }
    } else {
      neo_js_context_set_field(
          ctx, array, neo_js_context_create_number(ctx, *index), item, NULL);
      (*index) += 1;
    }
  }
  return NULL;
}

neo_js_variable_t neo_js_array_flat(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  double depth = 1;
  if (argc) {
    neo_js_variable_t v_depth = argv[0];
    v_depth = neo_js_context_to_integer(ctx, v_depth);
    NEO_JS_TRY_AND_THROW(v_depth);
    depth = neo_js_variable_to_number(v_depth)->number;
  }
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  double index = 0;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    if (neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_ARRAY &&
        depth >= 1) {
      neo_js_variable_t error =
          neo_js_array_flat_depth(ctx, array, &index, item, depth, 1);
      if (error) {
        return error;
      }
    } else {
      neo_js_context_set_field(
          ctx, array, neo_js_context_create_number(ctx, index), item, NULL);
      index += 1;
    }
  }
  return array;
}

neo_js_variable_t neo_js_array_flat_map(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;

  neo_js_variable_t array = neo_js_context_create_array(ctx);
  double index = 0;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    if (neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_ARRAY) {
      neo_js_variable_t vlength = neo_js_context_get_field(
          ctx, item, neo_js_context_create_string(ctx, "length"), NULL);
      NEO_JS_TRY_AND_THROW(vlength);
      vlength = neo_js_context_to_integer(ctx, vlength);
      NEO_JS_TRY_AND_THROW(vlength);
      double length = neo_js_variable_to_number(vlength)->number;
      for (double idx = 0; idx < length; idx += 1) {
        neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
        neo_js_variable_t item2 =
            neo_js_context_get_field(ctx, item, key, NULL);
        neo_js_variable_t args[] = {item2, key, item};
        item2 = neo_js_context_call(ctx, fn, this_arg, 3, args);
        NEO_JS_TRY_AND_THROW(item2);
        NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
            ctx, array, neo_js_context_create_number(ctx, index), item2, NULL));
        index += 1;
      }
    } else {
      neo_js_variable_t args[] = {item, key, self};
      item = neo_js_context_call(ctx, fn, this_arg, 3, args);
      NEO_JS_TRY_AND_THROW(item);
      NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
          ctx, array, neo_js_context_create_number(ctx, index), item, NULL));
      index += 1;
    }
  }
  return array;
}

neo_js_variable_t neo_js_array_for_each(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    NEO_JS_TRY_AND_THROW(neo_js_context_call(ctx, fn, this_arg, 3, args));
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_array_includes(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;

  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    if (neo_js_variable_get_type(item) == neo_js_variable_get_type(val)) {
      neo_js_variable_t res = neo_js_context_is_equal(ctx, val, item);
      if (neo_js_variable_to_boolean(res)->boolean) {
        return neo_js_context_create_boolean(ctx, true);
      }
    }
  }
  return neo_js_context_create_boolean(ctx, false);
}

neo_js_variable_t neo_js_array_index_of(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;

  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    if (neo_js_variable_get_type(item) == neo_js_variable_get_type(val)) {
      neo_js_variable_t res = neo_js_context_is_equal(ctx, val, item);
      if (neo_js_variable_to_boolean(res)->boolean) {
        return key;
      }
    }
  }
  return neo_js_context_create_number(ctx, -1);
}

neo_js_variable_t neo_js_array_join(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  neo_js_variable_t split = NULL;
  if (argc) {
    split = argv[0];
    split = neo_js_context_to_string(ctx, split);
    NEO_JS_TRY_AND_THROW(split);
  } else {
    split = neo_js_context_create_string(ctx, "");
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    if (idx != 0) {
      result = neo_js_context_concat(ctx, result, split);
      NEO_JS_TRY_AND_THROW(result);
    }
    if (neo_js_variable_get_type(item)->kind != NEO_JS_TYPE_UNDEFINED &&
        neo_js_variable_get_type(item)->kind != NEO_JS_TYPE_NULL) {
      result = neo_js_context_concat(ctx, result, item);
      NEO_JS_TRY_AND_THROW(result);
    }
  }
  return result;
}

neo_js_variable_t neo_js_array_keys(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_context_set_field(ctx, result, key, key, NULL);
  }
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "iterator"), NULL);
  iterator = neo_js_context_get_field(ctx, result, iterator, NULL);
  return neo_js_context_call(ctx, iterator, result, 0, NULL);
}

neo_js_variable_t neo_js_array_last_index_of(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {

  neo_js_variable_t val = NULL;
  if (argc > 0) {
    val = argv[0];
  } else {
    val = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;

  for (double idx = length - 1; idx >= 0; idx -= 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    if (neo_js_variable_get_type(item) == neo_js_variable_get_type(val)) {
      neo_js_variable_t res = neo_js_context_is_equal(ctx, val, item);
      if (neo_js_variable_to_boolean(res)->boolean) {
        return key;
      }
    }
  }
  return neo_js_context_create_number(ctx, -1);
}

neo_js_variable_t neo_js_array_map(neo_js_context_t ctx, neo_js_variable_t self,
                                   uint32_t argc, neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  } else {
    this_arg = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    item = neo_js_context_call(ctx, fn, this_arg, 3, args);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_context_set_field(ctx, result, key, result, NULL);
  }
  return result;
}

neo_js_variable_t neo_js_array_pop(neo_js_context_t ctx, neo_js_variable_t self,
                                   uint32_t argc, neo_js_variable_t *argv) {
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  if (length == 0) {
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t key = neo_js_context_create_number(ctx, length - 1);
  neo_js_variable_t res = neo_js_context_get_field(ctx, self, key, NULL);
  NEO_JS_TRY_AND_THROW(res);
  NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key));
  neo_js_variable_to_number(vlength)->number = length - 1;
  NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), vlength, NULL));
  return res;
}

neo_js_variable_t neo_js_array_push(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx + length);
    NEO_JS_TRY_AND_THROW(
        neo_js_context_set_field(ctx, self, key, argv[idx], NULL));
  }
  neo_js_variable_to_number(vlength)->number = length + argc;
  NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), vlength, NULL));
  return vlength;
}

neo_js_variable_t neo_js_array_reduce(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  if (length == 0 && argc < 2) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Reduce of empty array with no initial value");
  }
  if (length == 0) {
    return argv[1];
  }
  double idx = 1;
  neo_js_variable_t result = NULL;
  if (argc > 1) {
    result = argv[1];
    idx = 0;
  } else {
    result = neo_js_context_get_field(
        ctx, self, neo_js_context_create_number(ctx, 0), NULL);
  }
  while (idx < length) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    if (neo_js_context_has_field(ctx, self, key)) {
      neo_js_variable_t value = neo_js_context_get_field(ctx, self, key, NULL);
      NEO_JS_TRY_AND_THROW(value);
      neo_js_variable_t args[] = {result, value, key, self};
      result = neo_js_context_call(
          ctx, fn, neo_js_context_create_undefined(ctx), 4, args);
      NEO_JS_TRY_AND_THROW(result);
    }
    idx += 1;
  }
  return result;
}

neo_js_variable_t neo_js_array_reduce_right(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  if (length == 0 && argc < 2) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Reduce of empty array with no initial value");
  }
  if (length == 0) {
    return argv[1];
  }
  double idx = length - 2;
  neo_js_variable_t result = NULL;
  if (argc > 1) {
    result = argv[1];
    idx = length - 1;
  } else {
    result = neo_js_context_get_field(
        ctx, self, neo_js_context_create_number(ctx, length - 1), NULL);
  }
  while (idx >= 0) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    if (neo_js_context_has_field(ctx, self, key)) {
      neo_js_variable_t value = neo_js_context_get_field(ctx, self, key, NULL);
      NEO_JS_TRY_AND_THROW(value);
      neo_js_variable_t args[] = {result, value, key, self};
      result = neo_js_context_call(
          ctx, fn, neo_js_context_create_undefined(ctx), 4, args);
      NEO_JS_TRY_AND_THROW(result);
    }
    idx -= 1;
  }
  return result;
}

neo_js_variable_t neo_js_array_reverse(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  for (double_t idx = 0; idx <= length / 2; idx += 1) {
    neo_js_variable_t key_1 = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key_2 =
        neo_js_context_create_number(ctx, length - 1 - idx);
    neo_js_variable_t value_1 = NULL;
    neo_js_variable_t value_2 = NULL;
    if (neo_js_context_has_field(ctx, self, key_1)) {
      value_1 = neo_js_context_get_field(ctx, self, key_1, NULL);
      NEO_JS_TRY_AND_THROW(value_1);
    }
    if (neo_js_context_has_field(ctx, self, key_2)) {
      value_2 = neo_js_context_get_field(ctx, self, key_2, NULL);
      NEO_JS_TRY_AND_THROW(value_2);
    }
    if (value_1) {
      NEO_JS_TRY_AND_THROW(
          neo_js_context_set_field(ctx, self, key_2, value_1, NULL));
    } else {
      NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key_2));
    }
    if (value_2) {
      NEO_JS_TRY_AND_THROW(
          neo_js_context_set_field(ctx, self, key_1, value_2, NULL));
    } else {
      NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key_1));
    }
  }
  return self;
}

neo_js_variable_t neo_js_array_shift(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  if (length == 0) {
    return neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t key = neo_js_context_create_number(ctx, 0);
  neo_js_variable_t res = neo_js_context_get_field(ctx, self, key, NULL);
  NEO_JS_TRY_AND_THROW(res);
  NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key));
  for (double idx = 1; idx < length; idx += 1) {
    key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key_new = neo_js_context_create_number(ctx, idx - 1);
    if (neo_js_context_has_field(ctx, self, key)) {
      neo_js_variable_t value = neo_js_context_get_field(ctx, self, key, NULL);
      NEO_JS_TRY_AND_THROW(value);
      NEO_JS_TRY_AND_THROW(
          neo_js_context_set_field(ctx, self, key_new, value, NULL));
    } else {
      NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key_new));
    }
  }
  neo_js_variable_to_number(vlength)->number = length - 1;
  NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), vlength, NULL));
  return res;
}

neo_js_variable_t neo_js_array_slice(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  double start = 0;
  double end = 0;
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  if (length == 0) {
    return neo_js_context_create_array(ctx);
  }
  if (end < 0) {
    end += length;
  }
  if (end > length) {
    end = length;
  }
  if (start < 0) {
    start += length;
  }
  if (start < 0) {
    start = 0;
  }
  if (start >= length || end > length) {
    return neo_js_context_create_array(ctx);
  }
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  for (double idx = 0; idx < end - start; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx + start);
    neo_js_variable_t key_new = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    NEO_JS_TRY_AND_THROW(
        neo_js_context_set_field(ctx, array, key_new, item, NULL));
  }
  return array;
}

neo_js_variable_t neo_js_array_some(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  if (!argc) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "undefined is not a function");
  }
  neo_js_variable_t fn = argv[0];
  neo_js_variable_t this_arg = NULL;
  if (argc > 1) {
    this_arg = argv[1];
  }
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  for (double idx = 0; idx < length; idx++) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    neo_js_variable_t args[] = {item, key, self};
    neo_js_variable_t res = neo_js_context_call(ctx, fn, this_arg, 3, args);
    NEO_JS_TRY_AND_THROW(res);
    res = neo_js_context_to_boolean(ctx, res);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      return res;
    }
  }
  return neo_js_context_create_boolean(ctx, false);
}

static neo_js_variable_t
neo_js_array_quick_sort_part(neo_js_context_t ctx, neo_list_t list,
                             neo_list_node_t begin, neo_list_node_t end,
                             neo_js_variable_t compare) {
  if (begin == end || neo_list_node_next(begin) == end) {
    return neo_js_context_create_undefined(ctx);
  }
  neo_list_node_t pos = begin;
  neo_js_variable_t flag = neo_list_node_get(pos);
  neo_list_node_t it = neo_list_node_next(pos);
  begin = neo_list_node_last(begin);
  while (it != end) {
    neo_js_variable_t item = neo_list_node_get(it);
    neo_list_node_t next = neo_list_node_next(it);
    if (flag) {
      if (neo_js_variable_get_type(flag)->kind == NEO_JS_TYPE_UNDEFINED) {
        if (!item ||
            neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_UNDEFINED) {
          neo_list_move(list, pos, it);
        } else {
          neo_list_move(list, neo_list_node_last(pos), it);
        }
      } else {
        if (!item ||
            neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_UNDEFINED) {
          neo_list_move(list, pos, it);
        } else {
          double cmp = 0;
          if (compare) {
            neo_js_variable_t args[] = {flag, item};
            neo_js_variable_t res = neo_js_context_call(
                ctx, compare, neo_js_context_create_undefined(ctx), 2, args);
            NEO_JS_TRY_AND_THROW(res);
            res = neo_js_context_to_integer(ctx, res);
            cmp = neo_js_variable_to_number(res)->number;

          } else {
            neo_js_variable_t left = neo_js_context_to_string(ctx, flag);
            NEO_JS_TRY_AND_THROW(left);
            neo_js_variable_t right = neo_js_context_to_string(ctx, item);
            NEO_JS_TRY_AND_THROW(right);
            const char *s1 = neo_js_context_to_cstring(ctx, left);
            const char *s2 = neo_js_context_to_cstring(ctx, right);
            cmp = strcmp(s1, s2);
          }
          if (cmp <= 0) {
            neo_list_move(list, pos, it);
          } else {
            neo_list_move(list, neo_list_node_last(pos), it);
          }
        }
      }
    } else {
      if (!item) {
        neo_list_move(list, pos, it);
      } else {
        neo_list_move(list, neo_list_node_last(pos), it);
      }
    }
    it = next;
  }
  begin = neo_list_node_next(begin);
  NEO_JS_TRY_AND_THROW(
      neo_js_array_quick_sort_part(ctx, list, begin, pos, compare));
  NEO_JS_TRY_AND_THROW(neo_js_array_quick_sort_part(
      ctx, list, neo_list_node_next(pos), end, compare));
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t neo_js_array_quick_sort(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 double begin, double end,
                                                 neo_js_variable_t compare) {
  if (end - begin <= 1) {
    return self;
  }
  neo_js_variable_t flag = NULL;
  neo_js_variable_t key = neo_js_context_create_number(ctx, begin);
  if (neo_js_context_has_field(ctx, self, key)) {
    flag = neo_js_context_get_field(
        ctx, self, neo_js_context_create_number(ctx, begin), NULL);
    NEO_JS_TRY_AND_THROW(flag);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_list_t list = neo_create_list(allocator, NULL);
  neo_js_context_defer_free(ctx, list);
  neo_list_push(list, flag);
  neo_list_node_t it = neo_list_get_first(list);
  for (double index = 1; index < end - begin; index += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, index + begin);
    neo_js_variable_t item = NULL;
    if (neo_js_context_has_field(ctx, self, key)) {
      item = neo_js_context_get_field(ctx, self, key, NULL);
      NEO_JS_TRY_AND_THROW(item);
    }
    if (flag) {
      if (neo_js_variable_get_type(flag)->kind == NEO_JS_TYPE_UNDEFINED) {
        if (!item ||
            neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_UNDEFINED) {
          neo_list_insert(list, it, item);
        } else {
          neo_list_insert(list, neo_list_node_last(it), item);
        }
      } else {
        if (!item ||
            neo_js_variable_get_type(item)->kind == NEO_JS_TYPE_UNDEFINED) {
          neo_list_insert(list, it, item);
        } else {
          double cmp = 0;
          if (compare) {
            neo_js_variable_t args[] = {flag, item};
            neo_js_variable_t res = neo_js_context_call(
                ctx, compare, neo_js_context_create_undefined(ctx), 2, args);
            NEO_JS_TRY_AND_THROW(res);
            res = neo_js_context_to_integer(ctx, res);
            cmp = neo_js_variable_to_number(res)->number;

          } else {
            neo_js_variable_t left = neo_js_context_to_string(ctx, flag);
            NEO_JS_TRY_AND_THROW(left);
            neo_js_variable_t right = neo_js_context_to_string(ctx, item);
            NEO_JS_TRY_AND_THROW(right);
            const char *s1 = neo_js_context_to_cstring(ctx, left);
            const char *s2 = neo_js_context_to_cstring(ctx, right);
            cmp = strcmp(s1, s2);
          }
          if (cmp <= 0) {
            neo_list_insert(list, it, item);
          } else {
            neo_list_insert(list, neo_list_node_last(it), item);
          }
        }
      }
    } else {
      if (!item) {
        neo_list_insert(list, it, item);
      } else {
        neo_list_insert(list, neo_list_node_last(it), item);
      }
    }
  }
  NEO_JS_TRY_AND_THROW(neo_js_array_quick_sort_part(
      ctx, list, neo_list_get_first(list), it, compare));
  NEO_JS_TRY_AND_THROW(neo_js_array_quick_sort_part(
      ctx, list, neo_list_node_next(it), neo_list_get_tail(list), compare));
  double idx = 0;
  for (it = neo_list_get_first(list); it != neo_list_get_tail(list);
       it = neo_list_node_next(it)) {
    neo_js_variable_t item = neo_list_node_get(it);
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    if (item) {
      NEO_JS_TRY_AND_THROW(
          neo_js_context_set_field(ctx, self, key, item, NULL));
    } else {
      NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key));
    }
    idx += 1;
  }
  return self;
}

neo_js_variable_t neo_js_array_sort(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  neo_js_variable_t compare = NULL;
  if (argc > 0) {
    compare = argv[0];
  }
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  if (length <= 1) {
    return self;
  }
  NEO_JS_TRY_AND_THROW(neo_js_array_quick_sort(ctx, self, 0, length, compare));
  neo_js_variable_to_number(v_length)->number = length;
  NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), v_length, NULL));
  return self;
}

neo_js_variable_t neo_js_array_splice(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  double start = 0;
  double delete_count = 0;
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  if (argc > 0) {
    neo_js_variable_t v_start = argv[0];
    v_start = neo_js_context_to_integer(ctx, v_start);
    NEO_JS_TRY_AND_THROW(v_start);
    start = neo_js_variable_to_number(v_start)->number;
  }
  if (start < 0) {
    start += length;
  }
  if (start < 0) {
    start = 0;
  }
  if (argc > 1) {
    neo_js_variable_t v_delete_count = argv[0];
    v_delete_count = neo_js_context_to_integer(ctx, v_delete_count);
    NEO_JS_TRY_AND_THROW(v_delete_count);
    delete_count = neo_js_variable_to_number(v_delete_count)->number;
  } else {
    delete_count = length - start;
  }
  double insert_count = 0;
  if (argc > 2) {
    insert_count = argc - 2;
  }
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  for (double idx = 0; idx < delete_count; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx + start);
    if (neo_js_context_has_field(ctx, self, key)) {
      neo_js_variable_t key_new = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
      NEO_JS_TRY_AND_THROW(item);
      NEO_JS_TRY_AND_THROW(
          neo_js_context_set_field(ctx, result, key_new, item, NULL));
    }
  }
  double move_start = start + delete_count;
  double move_step = insert_count + delete_count;
  if (move_step < 0) {
    for (double idx = move_start; idx < length; idx += 1) {
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t key_new =
          neo_js_context_create_number(ctx, idx + move_step);
      if (neo_js_context_has_field(ctx, self, key)) {
        neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
        NEO_JS_TRY_AND_THROW(item);
        NEO_JS_TRY_AND_THROW(
            neo_js_context_set_field(ctx, self, key_new, item, NULL));
      } else {
        NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key_new));
      }
    }
  } else {
    for (double_t idx = length - 1; idx >= move_start; idx -= 1) {
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t key_new =
          neo_js_context_create_number(ctx, idx + move_step);
      if (neo_js_context_has_field(ctx, self, key)) {
        neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
        NEO_JS_TRY_AND_THROW(item);
        NEO_JS_TRY_AND_THROW(
            neo_js_context_set_field(ctx, self, key_new, item, NULL));
      } else {
        NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key_new));
      }
    }
  }
  for (double idx = 2; idx < argc; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx - 2 + start);
    NEO_JS_TRY_AND_THROW(
        neo_js_context_set_field(ctx, self, key, argv[(uint32_t)idx], NULL));
  }
  neo_js_variable_to_number(v_length)->number =
      length - delete_count + insert_count;
  NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), v_length, NULL));
  return result;
}

neo_js_variable_t neo_js_array_to_local_string(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  return neo_js_array_to_string(ctx, self, argc, argv);
}

neo_js_variable_t neo_js_array_to_reversed(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t key_new =
        neo_js_context_create_number(ctx, length - 1 - idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    NEO_JS_TRY_AND_THROW(
        neo_js_context_set_field(ctx, result, key_new, item, NULL));
  }
  return result;
}

neo_js_variable_t neo_js_array_to_sorted(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    NEO_JS_TRY_AND_THROW(
        neo_js_context_set_field(ctx, result, key, item, NULL));
  }
  NEO_JS_TRY_AND_THROW(neo_js_array_sort(ctx, result, argc, argv));
  return result;
}

neo_js_variable_t neo_js_array_to_spliced(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    NEO_JS_TRY_AND_THROW(
        neo_js_context_set_field(ctx, result, key, item, NULL));
  }
  NEO_JS_TRY_AND_THROW(neo_js_array_splice(ctx, result, argc, argv));
  return result;
}

neo_js_variable_t neo_js_array_to_string(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t split = neo_js_context_create_string(ctx, ",");
  return neo_js_array_join(ctx, self, 1, &split);
}

neo_js_variable_t neo_js_array_unshift(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  neo_js_variable_t value = NULL;
  if (argc > 0) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  double length = neo_js_variable_to_number(vlength)->number;
  for (double idx = length; idx > 0; idx -= 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx - 1);
    neo_js_variable_t key_new = neo_js_context_create_number(ctx, idx);
    if (neo_js_context_has_field(ctx, self, key)) {
      neo_js_variable_t value = neo_js_context_get_field(ctx, self, key, NULL);
      NEO_JS_TRY_AND_THROW(value);
      NEO_JS_TRY_AND_THROW(
          neo_js_context_set_field(ctx, self, key_new, value, NULL));
    } else {
      NEO_JS_TRY_AND_THROW(neo_js_context_del_field(ctx, self, key_new));
    }
  }
  NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
      ctx, self, neo_js_context_create_number(ctx, 0), value, NULL));
  neo_js_variable_to_number(vlength)->number = length + 1;
  NEO_JS_TRY_AND_THROW(neo_js_context_set_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), vlength, NULL));
  return vlength;
}

neo_js_variable_t neo_js_array_values(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  neo_js_variable_t iterator = neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).array_iterator_constructor, 0, NULL);
  neo_js_variable_t index = neo_js_context_create_number(ctx, 0);
  neo_js_context_set_internal(ctx, iterator, "[[array]]", self);
  neo_js_context_set_internal(ctx, iterator, "[[index]]", index);
  return iterator;
}

neo_js_variable_t neo_js_array_with(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  neo_js_variable_t v_length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(v_length);
  v_length = neo_js_context_to_integer(ctx, v_length);
  NEO_JS_TRY_AND_THROW(v_length);
  double length = neo_js_variable_to_number(v_length)->number;
  for (double idx = 0; idx < length; idx += 1) {
    neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t item = neo_js_context_get_field(ctx, self, key, NULL);
    NEO_JS_TRY_AND_THROW(item);
    NEO_JS_TRY_AND_THROW(
        neo_js_context_set_field(ctx, result, key, item, NULL));
  }
  if (argc > 0) {
    double idx = 0;
    neo_js_variable_t v_index = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(v_index);
    idx = neo_js_variable_to_number(v_index)->number;
    if (idx < 0) {
      idx += length;
    }
    if (idx < 0 || idx >= length) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE, 0,
                                                "Invalid array index");
    }
    neo_js_variable_t value = NULL;
    if (argc > 1) {
      value = argv[1];
    } else {
      value = neo_js_context_create_undefined(ctx);
    }
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_number(ctx, idx), value, NULL);
  }
  return result;
}

void neo_js_context_init_std_array(neo_js_context_t ctx) {

  neo_js_variable_t from =
      neo_js_context_create_cfunction(ctx, "from", neo_js_array_from);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).array_constructor,
                           neo_js_context_create_string(ctx, "from"), from,
                           true, false, true);

  neo_js_variable_t from_async = neo_js_context_create_async_cfunction(
      ctx, "fromAsync", neo_js_array_from_async);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).array_constructor,
                           neo_js_context_create_string(ctx, "fromAsync"),
                           from_async, true, false, true);

  neo_js_variable_t is_array =
      neo_js_context_create_cfunction(ctx, "isArray", neo_js_array_is_array);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).array_constructor,
                           neo_js_context_create_string(ctx, "isArray"),
                           is_array, true, false, true);

  neo_js_variable_t of =
      neo_js_context_create_cfunction(ctx, "of", neo_js_array_of);
  neo_js_context_def_field(ctx, neo_js_context_get_std(ctx).array_constructor,
                           neo_js_context_create_string(ctx, "of"), of, true,
                           false, true);

  neo_js_variable_t species = neo_js_context_create_cfunction(
      ctx, "[Symbol.species]", neo_js_array_species);
  neo_js_context_def_accessor(
      ctx, neo_js_context_get_std(ctx).array_constructor,
      neo_js_context_get_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor,
          neo_js_context_create_string(ctx, "species"), NULL),
      species, NULL, true, false);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).array_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL);

  neo_js_variable_t at =
      neo_js_context_create_cfunction(ctx, "at", neo_js_array_at);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "at"), at, true,
                           false, true);

  neo_js_variable_t concat =
      neo_js_context_create_cfunction(ctx, "concat", neo_js_array_concat);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "concat"), concat,
                           true, false, true);

  neo_js_variable_t copy_within = neo_js_context_create_cfunction(
      ctx, "copyWithin", neo_js_array_copy_within);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "copyWithin"),
                           copy_within, true, false, true);

  neo_js_variable_t entries =
      neo_js_context_create_cfunction(ctx, "entries", neo_js_array_entries);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "entries"),
                           entries, true, false, true);

  neo_js_variable_t every =
      neo_js_context_create_cfunction(ctx, "every", neo_js_array_every);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "every"), every,
                           true, false, true);

  neo_js_variable_t fill =
      neo_js_context_create_cfunction(ctx, "fill", neo_js_array_fill);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "fill"), fill,
                           true, false, true);

  neo_js_variable_t filter =
      neo_js_context_create_cfunction(ctx, "filter", neo_js_array_filter);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "filter"), filter,
                           true, false, true);

  neo_js_variable_t find =
      neo_js_context_create_cfunction(ctx, "find", neo_js_array_find);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "find"), find,
                           true, false, true);

  neo_js_variable_t find_index = neo_js_context_create_cfunction(
      ctx, "findIndex", neo_js_array_find_index);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "findIndex"),
                           find_index, true, false, true);

  neo_js_variable_t find_last =
      neo_js_context_create_cfunction(ctx, "findLast", neo_js_array_find_last);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "findLast"),
                           find_last, true, false, true);

  neo_js_variable_t find_last_index = neo_js_context_create_cfunction(
      ctx, "findLastIndex", neo_js_array_find_last_index);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "findLastIndex"),
                           find_last_index, true, false, true);

  neo_js_variable_t flat =
      neo_js_context_create_cfunction(ctx, "flat", neo_js_array_flat);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "flat"), flat,
                           true, false, true);

  neo_js_variable_t flat_map =
      neo_js_context_create_cfunction(ctx, "flatMap", neo_js_array_flat_map);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "flat_map"),
                           flat_map, true, false, true);

  neo_js_variable_t for_each =
      neo_js_context_create_cfunction(ctx, "forEach", neo_js_array_for_each);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "forEach"),
                           for_each, true, false, true);

  neo_js_variable_t includes =
      neo_js_context_create_cfunction(ctx, "includes", neo_js_array_includes);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "includes"),
                           includes, true, false, true);

  neo_js_variable_t index_of =
      neo_js_context_create_cfunction(ctx, "indexOf", neo_js_array_index_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "indexOf"),
                           index_of, true, false, true);

  neo_js_variable_t join =
      neo_js_context_create_cfunction(ctx, "join", neo_js_array_join);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "join"), join,
                           true, false, true);

  neo_js_variable_t keys =
      neo_js_context_create_cfunction(ctx, "keys", neo_js_array_keys);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "keys"), keys,
                           true, false, true);

  neo_js_variable_t last_index_of = neo_js_context_create_cfunction(
      ctx, "lastIndexOf", neo_js_array_last_index_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "lastIndexOf"),
                           last_index_of, true, false, true);

  neo_js_variable_t map =
      neo_js_context_create_cfunction(ctx, "map", neo_js_array_map);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "map"), map, true,
                           false, true);

  neo_js_variable_t pop =
      neo_js_context_create_cfunction(ctx, "pop", neo_js_array_pop);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "pop"), pop, true,
                           false, true);

  neo_js_variable_t push =
      neo_js_context_create_cfunction(ctx, "push", neo_js_array_push);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "push"), push,
                           true, false, true);

  neo_js_variable_t reduce =
      neo_js_context_create_cfunction(ctx, "reduce", neo_js_array_reduce);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "reduce"), reduce,
                           true, false, true);

  neo_js_variable_t reduce_right = neo_js_context_create_cfunction(
      ctx, "reduceRight", neo_js_array_reduce_right);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "reduceRight"),
                           reduce_right, true, false, true);

  neo_js_variable_t reverse =
      neo_js_context_create_cfunction(ctx, "reverse", neo_js_array_reverse);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "reverse"),
                           reverse, true, false, true);

  neo_js_variable_t shift =
      neo_js_context_create_cfunction(ctx, "shift", neo_js_array_shift);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "shift"), shift,
                           true, false, true);

  neo_js_variable_t slice =
      neo_js_context_create_cfunction(ctx, "slice", neo_js_array_slice);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "slice"), slice,
                           true, false, true);

  neo_js_variable_t some =
      neo_js_context_create_cfunction(ctx, "some", neo_js_array_some);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "some"), some,
                           true, false, true);

  neo_js_variable_t sort =
      neo_js_context_create_cfunction(ctx, "sort", neo_js_array_sort);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "sort"), sort,
                           true, false, true);

  neo_js_variable_t splice =
      neo_js_context_create_cfunction(ctx, "splice", neo_js_array_splice);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "splice"), splice,
                           true, false, true);

  neo_js_variable_t to_local_string = neo_js_context_create_cfunction(
      ctx, "toLocalString", neo_js_array_to_local_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "toLocalString"),
                           to_local_string, true, false, true);

  neo_js_variable_t to_reversed = neo_js_context_create_cfunction(
      ctx, "toReversed", neo_js_array_to_reversed);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "toReversed"),
                           to_reversed, true, false, true);

  neo_js_variable_t to_sorted =
      neo_js_context_create_cfunction(ctx, "toSorted", neo_js_array_to_sorted);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "toSorted"),
                           to_sorted, true, false, true);

  neo_js_variable_t to_spliced = neo_js_context_create_cfunction(
      ctx, "toSpliced", neo_js_array_to_spliced);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "toSpliced"),
                           to_spliced, true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "toString"),
      neo_js_context_create_cfunction(ctx, "toString", neo_js_array_to_string),
      true, false, true);

  neo_js_variable_t unshift =
      neo_js_context_create_cfunction(ctx, "unshift", neo_js_array_unshift);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "unshift"),
                           unshift, true, false, true);

  neo_js_variable_t values =
      neo_js_context_create_cfunction(ctx, "values", neo_js_array_values);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "values"), values,
                           true, false, true);

  neo_js_variable_t with =
      neo_js_context_create_cfunction(ctx, "with", neo_js_array_with);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "with"), with,
                           true, false, true);

  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "iterator"), NULL);
  neo_js_context_def_field(ctx, prototype, iterator, values, true, false, true);
}