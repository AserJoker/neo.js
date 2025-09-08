#include "engine/std/bigint.h"
#include "core/allocator.h"
#include "core/bigint.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/number.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <string.h>
#include <wchar.h>
neo_js_variable_t neo_js_bigint_as_int_n(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t v_width = NULL;
  neo_js_variable_t v_input = NULL;
  if (argc > 0) {
    v_width = argv[0];
  } else {
    v_width = neo_js_context_create_undefined(ctx);
  }
  if (argc > 1) {
    v_input = argv[1];
  } else {
    v_input = neo_js_context_create_undefined(ctx);
  }
  v_width = neo_js_context_to_integer(ctx, v_width);
  NEO_JS_TRY_AND_THROW(v_width);
  v_input = neo_js_context_to_primitive(ctx, v_input, "bigint");
  NEO_JS_TRY_AND_THROW(v_input);
  if (neo_js_variable_get_type(v_input)->kind != NEO_JS_TYPE_BIGINT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_SYNTAX, 0, "cannot convert variable to bigint");
  }
  double width = neo_js_variable_to_number(v_width)->number;
  neo_bigint_t bigint = neo_js_variable_to_bigint(v_input)->bigint;
  neo_bigint_t input = bigint;
  if (neo_bigint_is_negative(bigint)) {
    input = neo_bigint_neg(input);
    neo_js_context_defer_free(ctx, input);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_bigint_t max = neo_number_to_bigint(allocator, pow(2, width));
  neo_js_context_defer_free(ctx, max);
  neo_bigint_t max_sub = neo_number_to_bigint(allocator, pow(2, width - 1));
  neo_js_context_defer_free(ctx, max_sub);
  neo_bigint_t result = neo_bigint_mod(input, max);
  if (neo_bigint_is_greater_or_equal(result, max_sub)) {
    neo_js_context_defer_free(ctx, result);
    result = neo_bigint_sub(result, max);
  }
  if (neo_bigint_is_negative(bigint)) {
    neo_js_context_defer_free(ctx, result);
    result = neo_bigint_neg(result);
  }
  return neo_js_context_create_bigint(ctx, result);
}

neo_js_variable_t neo_js_bigint_as_uint_n(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_variable_t v_width = NULL;
  neo_js_variable_t v_input = NULL;
  if (argc > 0) {
    v_width = argv[0];
  } else {
    v_width = neo_js_context_create_undefined(ctx);
  }
  if (argc > 1) {
    v_input = argv[1];
  } else {
    v_input = neo_js_context_create_undefined(ctx);
  }
  v_width = neo_js_context_to_integer(ctx, v_width);
  NEO_JS_TRY_AND_THROW(v_width);
  v_input = neo_js_context_to_primitive(ctx, v_input, "bigint");
  NEO_JS_TRY_AND_THROW(v_input);
  if (neo_js_variable_get_type(v_input)->kind != NEO_JS_TYPE_BIGINT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_SYNTAX, 0, "cannot convert variable to bigint");
  }
  double width = neo_js_variable_to_number(v_width)->number;
  neo_bigint_t bigint = neo_js_variable_to_bigint(v_input)->bigint;
  neo_bigint_t input = bigint;
  if (neo_bigint_is_negative(bigint)) {
    input = neo_bigint_neg(input);
    neo_js_context_defer_free(ctx, input);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_bigint_t max = neo_number_to_bigint(allocator, pow(2, width));
  neo_js_context_defer_free(ctx, max);
  neo_bigint_t result = neo_bigint_mod(input, max);
  if (neo_bigint_is_negative(bigint)) {
    neo_js_context_defer_free(ctx, result);
    result = neo_bigint_sub(max, result);
  }
  return neo_js_context_create_bigint(ctx, result);
}

neo_js_variable_t neo_js_bigint_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  if (neo_js_context_get_call_type(ctx) == NEO_JS_CONSTRUCT_CALL) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "BigInt is not a constructor");
  }
  if (argc == 0 ||
      neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Cannot convert undefined to a BigInt");
  }
  if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_NULL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Cannot convert null to a BigInt");
  }
  neo_js_variable_t variable = neo_js_context_to_primitive(ctx, argv[0], NULL);
  NEO_JS_TRY_AND_THROW(variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_SYMBOL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0, "Cannot convert symbol to a BigInt");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_NUMBER) {
    neo_js_number_t num = neo_js_variable_to_number(variable);
    neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
    return neo_js_context_create_bigint(ctx, bigint);
  } else if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_BOOLEAN) {
    neo_js_boolean_t boolean = neo_js_variable_to_boolean(variable);
    neo_bigint_t bigint = neo_number_to_bigint(allocator, boolean->boolean);
    return neo_js_context_create_bigint(ctx, bigint);
  } else {
    variable = neo_js_context_to_string(ctx, variable);
    NEO_JS_TRY_AND_THROW(variable);
    neo_js_string_t string = neo_js_variable_to_string(variable);
    neo_bigint_t bigint = neo_string_to_bigint(allocator, string->string);
    if (!bigint) {
      size_t len = strlen(string->string) + 64;
      char *message = neo_allocator_alloc(allocator, len * sizeof(char), NULL);
      neo_js_context_defer_free(ctx, message);
      snprintf(message, len, "Cannot convert %s to a BigInt", string->string);
    }
    return neo_js_context_create_bigint(ctx, bigint);
  }
}

neo_js_variable_t neo_js_bigint_to_string(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  uint32_t radix = 10;
  if (argc > 0) {
    neo_js_variable_t v_radix = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(v_radix);
    radix = neo_js_variable_to_number(v_radix)->number;
    if (radix > 36 || radix < 2) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_RANGE, 0,
          "toString() radix argument must be between 2 and 36");
    }
  }
  neo_js_variable_t primitive =
      neo_js_context_get_internal(ctx, self, "[[primitive]]");
  if (neo_js_variable_get_type(primitive)->kind != NEO_JS_TYPE_BIGINT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "BigInt.prototype.valueOf requires that 'this' be a BigInt");
  }
  neo_bigint_t bigint = neo_js_variable_to_bigint(primitive)->bigint;
  char *str = neo_bigint_to_string(bigint, radix);
  neo_js_context_defer_free(ctx, str);
  return neo_js_context_create_string(ctx, str);
}

neo_js_variable_t neo_js_bigint_to_local_string(neo_js_context_t ctx,
                                                neo_js_variable_t self,
                                                uint32_t argc,
                                                neo_js_variable_t *argv) {
  return neo_js_bigint_to_string(ctx, self, argc, argv);
}

neo_js_variable_t neo_js_bigint_value_of(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t primitive =
      neo_js_context_get_internal(ctx, self, "[[primitive]]");
  if (neo_js_variable_get_type(primitive)->kind != NEO_JS_TYPE_BIGINT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "BigInt.prototype.valueOf requires that 'this' be a BigInt");
  }
  return primitive;
}
void neo_js_context_init_std_bigint(neo_js_context_t ctx) {
  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).bigint_constructor,
      neo_js_context_create_string(ctx, "asIntN"),
      neo_js_context_create_cfunction(ctx, "asIntN", neo_js_bigint_as_int_n),
      true, false, true);

  neo_js_context_def_field(
      ctx, neo_js_context_get_std(ctx).bigint_constructor,
      neo_js_context_create_string(ctx, "asUintN"),
      neo_js_context_create_cfunction(ctx, "asUintN", neo_js_bigint_as_uint_n),
      true, false, true);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).bigint_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL);

  neo_js_variable_t value_of =
      neo_js_context_create_cfunction(ctx, "valueOf", neo_js_bigint_value_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "valueOf"),
                           value_of, true, false, true);

  neo_js_variable_t to_string =
      neo_js_context_create_cfunction(ctx, "toString", neo_js_bigint_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "toString"),
                           to_string, true, false, true);

  neo_js_variable_t to_local_string = neo_js_context_create_cfunction(
      ctx, "toLocalString", neo_js_bigint_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "toLocalString"),
                           to_local_string, true, false, true);
}