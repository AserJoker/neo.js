#include "neojs/runtime/bigint.h"
#include "neojs/core/allocator.h"
#include "neojs/core/bigint.h"
#include "neojs/engine/context.h"
#include "neojs/engine/number.h"
#include "neojs/engine/string.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"

NEO_JS_CFUNCTION(neo_js_bigint_constructor) {
  if (neo_js_context_get_type(ctx) == NEO_JS_CONTEXT_CONSTRUCT) {
    neo_js_variable_t message =
        neo_js_context_create_cstring(ctx, "BigInt is not a constructor");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  } else {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_bigint_t bigint = NULL;
    if (argc) {
      neo_js_variable_t src = neo_js_variable_to_string(argv[0], ctx);
      if (src->value->type == NEO_JS_TYPE_EXCEPTION) {
        return src;
      }
      bigint = neo_string16_to_bigint(allocator,
                                      ((neo_js_string_t)src->value)->value);
      if (!bigint) {
        neo_js_variable_t message =
            neo_js_context_format(ctx, "Cannot convert %v to a BigInt", src);
        neo_js_variable_t error = neo_js_variable_construct(
            neo_js_context_get_constant(ctx)->type_error_class, ctx, 1,
            &message);
        return neo_js_context_create_exception(ctx, error);
      }
    } else {
      bigint = neo_create_bigint(allocator);
    }
    return neo_js_context_create_bigint(ctx, bigint);
  }
}
NEO_JS_CFUNCTION(neo_js_bigint_as_int_n) {
  neo_js_variable_t width = neo_js_context_get_argument(ctx, argc, argv, 0);
  width = neo_js_variable_to_integer(width, ctx);
  if (width->value->type == NEO_JS_TYPE_EXCEPTION) {
    return width;
  }
  double w = ((neo_js_number_t)width->value)->value;
  if (w < 0 || w >= (double)((int64_t)2 << 53) - 1) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Cannot convert width to safe integer");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->range_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  value = neo_js_variable_to_string(value, ctx);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value;
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_bigint_t target =
      neo_string16_to_bigint(allocator, ((neo_js_string_t)value->value)->value);
  if (!target) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot convert %v to a BigInt", target);
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_bigint_t tmp = neo_number_to_bigint(allocator, 2);
  neo_bigint_t tmp1 = neo_number_to_bigint(allocator, (int64_t)w);
  neo_bigint_t max = neo_bigint_pow(tmp, tmp1);
  neo_allocator_free(allocator, tmp1);
  neo_allocator_free(allocator, tmp);
  neo_bigint_t min = neo_bigint_neg(max);
  if (neo_bigint_is_greater_or_equal(target, max)) {
    neo_allocator_free(allocator, target);
    target = neo_create_bigint(allocator);
  }
  if (neo_bigint_is_less_or_equal(target, min)) {
    neo_allocator_free(allocator, target);
    target = neo_create_bigint(allocator);
  }
  neo_allocator_free(allocator, max);
  neo_allocator_free(allocator, min);
  return neo_js_context_create_bigint(ctx, target);
}
NEO_JS_CFUNCTION(neo_js_bigint_as_uint_n) {

  neo_js_variable_t width = neo_js_context_get_argument(ctx, argc, argv, 0);
  width = neo_js_variable_to_integer(width, ctx);
  if (width->value->type == NEO_JS_TYPE_EXCEPTION) {
    return width;
  }
  double w = ((neo_js_number_t)width->value)->value;
  if (w < 0 || w >= (double)((int64_t)2 << 53) - 1) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Cannot convert width to safe integer");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->range_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  value = neo_js_variable_to_string(value, ctx);
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    return value;
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_bigint_t target =
      neo_string16_to_bigint(allocator, ((neo_js_string_t)value->value)->value);
  if (!target) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "Cannot convert %v to a BigInt", target);
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_bigint_t tmp = neo_number_to_bigint(allocator, 2);
  neo_bigint_t tmp1 = neo_number_to_bigint(allocator, (int64_t)w);
  neo_bigint_t max = neo_bigint_pow(tmp, tmp1);
  neo_allocator_free(allocator, tmp1);
  neo_allocator_free(allocator, tmp);
  neo_bigint_t min = neo_create_bigint(allocator);
  if (neo_bigint_is_greater_or_equal(target, max)) {
    neo_allocator_free(allocator, target);
    target = neo_create_bigint(allocator);
  }
  if (neo_bigint_is_less_or_equal(target, min)) {
    neo_allocator_free(allocator, target);
    target = neo_create_bigint(allocator);
  }
  neo_allocator_free(allocator, max);
  neo_allocator_free(allocator, min);
  return neo_js_context_create_bigint(ctx, target);
}
NEO_JS_CFUNCTION(neo_js_bigint_to_locale_string) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  if (value->value->type != NEO_JS_TYPE_BIGINT) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx,
        "BigInt.prototype.toLocaleString requires that 'this' be a BigInt");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t number_format = neo_js_variable_get_field(
      constant->intl, ctx, neo_js_context_create_cstring(ctx, "NumberFormat"));
  neo_js_variable_t res =
      neo_js_variable_call(number_format, ctx, constant->intl, argc, argv);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    return res;
  }
  neo_js_variable_t format = neo_js_variable_get_field(
      res, ctx, neo_js_context_create_cstring(ctx, "format"));
  return neo_js_variable_call(format, ctx, res, 1, &value);
}
NEO_JS_CFUNCTION(neo_js_bigint_to_string) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  if (value->value->type != NEO_JS_TYPE_BIGINT) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "BigInt.prototype.toString requires that 'this' be a BigInt");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  return neo_js_variable_to_string(value, ctx);
}
NEO_JS_CFUNCTION(neo_js_bigint_value_of) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  if (value->value->type != NEO_JS_TYPE_BIGINT) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "BigInt.prototype.valueOf requires that 'this' be a BigInt");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  return value;
}
void neo_initialize_js_bigint(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->bigint_class =
      neo_js_context_create_cfunction(ctx, neo_js_bigint_constructor, "BigInt");
  constant->bigint_prototype = neo_js_variable_get_field(
      constant->bigint_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->bigint_class, "asIntN",
                    neo_js_bigint_as_int_n);
  NEO_JS_DEF_METHOD(ctx, constant->bigint_class, "asUIntN",
                    neo_js_bigint_as_uint_n);
  NEO_JS_DEF_METHOD(ctx, constant->bigint_prototype, "toLocaleString",
                    neo_js_bigint_to_locale_string);
  NEO_JS_DEF_METHOD(ctx, constant->bigint_prototype, "toString",
                    neo_js_bigint_to_string);
  NEO_JS_DEF_METHOD(ctx, constant->bigint_prototype, "valueOf",
                    neo_js_bigint_value_of);
}