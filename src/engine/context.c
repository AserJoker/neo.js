#include "engine/context.h"
#include "compiler/ast/node.h"
#include "compiler/parser.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/clock.h"
#include "core/error.h"
#include "core/hash_map.h"
#include "core/json.h"
#include "core/list.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/basetype/array.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/callable.h"
#include "engine/basetype/cfunction.h"
#include "engine/basetype/error.h"
#include "engine/basetype/function.h"
#include "engine/basetype/interrupt.h"
#include "engine/basetype/null.h"
#include "engine/basetype/number.h"
#include "engine/basetype/object.h"
#include "engine/basetype/string.h"
#include "engine/basetype/symbol.h"
#include "engine/basetype/undefined.h"
#include "engine/basetype/uninitialize.h"
#include "engine/coroutine.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/stackframe.h"
#include "engine/std/array.h"
#include "engine/std/array_iterator.h"
#include "engine/std/async_function.h"
#include "engine/std/async_generator.h"
#include "engine/std/async_generator_function.h"
#include "engine/std/error.h"
#include "engine/std/function.h"
#include "engine/std/generator.h"
#include "engine/std/generator_function.h"
#include "engine/std/object.h"
#include "engine/std/promise.h"
#include "engine/std/range_error.h"
#include "engine/std/reference_error.h"
#include "engine/std/symbol.h"
#include "engine/std/syntax_error.h"
#include "engine/std/type_error.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/vm.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>

typedef struct _neo_js_task_t *neo_js_task_t;

struct _neo_js_task_t {
  neo_js_handle_t function;
  uint64_t start;
  uint64_t timeout;
  uint32_t id;
  uint32_t argc;
  neo_js_handle_t *argv;
  neo_js_handle_t self;
  bool keepalive;
};

struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t scope;
  neo_js_scope_t root;
  neo_js_scope_t task;
  neo_list_t stacktrace;
  neo_list_t micro_tasks;
  neo_list_t macro_tasks;
  neo_list_t coroutines;
  struct {
    neo_js_variable_t global;
    neo_js_variable_t object_constructor;
    neo_js_variable_t function_constructor;
    neo_js_variable_t async_function_constructor;
    neo_js_variable_t number_constructor;
    neo_js_variable_t boolean_constructor;
    neo_js_variable_t string_constructor;
    neo_js_variable_t symbol_constructor;
    neo_js_variable_t array_constructor;
    neo_js_variable_t bigint_constructor;
    neo_js_variable_t regexp_constructor;
    neo_js_variable_t iterator_constructor;
    neo_js_variable_t array_iterator_constructor;
    neo_js_variable_t generator_constructor;
    neo_js_variable_t generator_function_constructor;
    neo_js_variable_t async_generator_constructor;
    neo_js_variable_t async_generator_function_constructor;
    neo_js_variable_t promise_constructor;
    neo_js_variable_t error_constructor;
    neo_js_variable_t syntax_error_constructor;
    neo_js_variable_t type_error_constructor;
    neo_js_variable_t reference_error_constructor;
    neo_js_variable_t range_error_constructor;
  } std;
};

static void neo_js_context_init_std_symbol(neo_js_context_t ctx) {
  neo_js_context_push_scope(ctx);
  neo_js_variable_t async_iterator =
      neo_js_context_create_symbol(ctx, L"asyncIterator");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"asyncIterator"),
                           async_iterator, true, false, true);

  neo_js_variable_t has_instance =
      neo_js_context_create_symbol(ctx, L"hasInstance");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"hasInstance"),
                           has_instance, true, false, true);

  neo_js_variable_t is_concat_spreadable =
      neo_js_context_create_symbol(ctx, L"isConcatSpreadable");
  neo_js_context_def_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"isConcatSpreadable"),
      is_concat_spreadable, true, false, true);

  neo_js_variable_t iterator = neo_js_context_create_symbol(ctx, L"iterator");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"iterator"),
                           iterator, true, false, true);

  neo_js_variable_t match = neo_js_context_create_symbol(ctx, L"match");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"match"), match,
                           true, false, true);

  neo_js_variable_t match_all = neo_js_context_create_symbol(ctx, L"matchAll");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"matchAll"),
                           match_all, true, false, true);

  neo_js_variable_t replace = neo_js_context_create_symbol(ctx, L"replace");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"replace"),
                           replace, true, false, true);

  neo_js_variable_t search = neo_js_context_create_symbol(ctx, L"search");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"search"), search,
                           true, false, true);

  neo_js_variable_t species = neo_js_context_create_symbol(ctx, L"species");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"species"),
                           species, true, false, true);

  neo_js_variable_t split = neo_js_context_create_symbol(ctx, L"split");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"split"), split,
                           true, false, true);

  neo_js_variable_t to_primitive =
      neo_js_context_create_symbol(ctx, L"toPrimitive");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"toPrimitive"),
                           to_primitive, true, false, true);

  neo_js_variable_t to_string_tag =
      neo_js_context_create_symbol(ctx, L"toStringTag");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"toStringTag"),
                           to_string_tag, true, false, true);

  neo_js_variable_t for_ =
      neo_js_context_create_cfunction(ctx, L"for", &neo_js_symbol_for);
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"for"), for_,
                           true, false, true);

  neo_js_variable_t key_for =
      neo_js_context_create_cfunction(ctx, L"keyFor", &neo_js_symbol_key_for);
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"keyFor"),
                           key_for, true, false, true);

  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_variable_t to_string = neo_js_context_create_cfunction(
      ctx, L"toString", &neo_js_symbol_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           to_string, true, false, true);

  neo_js_variable_t value_of =
      neo_js_context_create_cfunction(ctx, L"valueOf", &neo_js_symbol_value_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"valueOf"),
                           value_of, true, false, true);

  neo_js_variable_t to_primitive_func = neo_js_context_create_cfunction(
      ctx, L"[Symbol.toPrimitive]", &neo_js_symbol_to_primitive);

  neo_js_context_def_field(ctx, prototype, to_primitive, to_primitive_func,
                           true, false, true);
  neo_js_context_pop_scope(ctx);
}

static void neo_js_context_init_std_object(neo_js_context_t ctx) {

  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"keys"),
      neo_js_context_create_cfunction(ctx, L"keys", neo_js_object_keys), true,
      false, true);

  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"create"),
      neo_js_context_create_cfunction(ctx, L"create", neo_js_object_create),
      true, false, true);

  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.object_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"valueOf"),
      neo_js_context_create_cfunction(ctx, L"valueOf", neo_js_object_value_of),
      true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           neo_js_context_create_cfunction(
                               ctx, L"toString", neo_js_object_to_string),
                           true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toLocalString"),
      neo_js_context_create_cfunction(ctx, L"toLocalString",
                                      neo_js_object_to_local_string),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"hasOwnProperty"),
      neo_js_context_create_cfunction(ctx, L"hasOwnProperty",
                                      neo_js_object_has_own_property),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"isPrototypeOf"),
      neo_js_context_create_cfunction(ctx, L"isPrototypeOf",
                                      neo_js_object_is_prototype_of),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"isPrototypeOf"),
      neo_js_context_create_cfunction(ctx, L"isPrototypeOf",
                                      neo_js_object_property_is_enumerable),
      true, false, true);
}

static void neo_js_context_init_std_function(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.function_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           neo_js_context_create_cfunction(
                               ctx, L"toString", neo_js_function_to_string),
                           true, false, true);
}

static void neo_js_context_init_std_array(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.array_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString", neo_js_array_to_string),
      true, false, true);

  neo_js_variable_t values =
      neo_js_context_create_cfunction(ctx, L"values", neo_js_array_values);
  neo_js_context_set_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"values"), values);

  neo_js_variable_t iterator =
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"iterator"));
  neo_js_context_set_field(ctx, prototype, iterator, values);
}

static void neo_js_context_init_std_generator_function(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.generator_function_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString",
                                      neo_js_generator_function_to_string),
      true, false, true);
}
static void
neo_js_context_init_std_async_generator_function(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.async_generator_function_constructor,
      neo_js_context_create_string(ctx, L"prototype"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(
          ctx, L"toString", neo_js_async_generator_function_to_string),
      true, false, true);
}
static void neo_js_context_init_std_async_function(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.async_function_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString",
                                      neo_js_async_function_to_string),
      true, false, true);
}
static void neo_js_context_init_std_generator(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.generator_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_variable_t to_string_tag = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"toStringTag"));

  neo_js_context_set_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, L"Generator"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"next"),
      neo_js_context_create_cfunction(ctx, L"next", neo_js_generator_next),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"return"),
      neo_js_context_create_cfunction(ctx, L"return", neo_js_generator_return),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"throw"),
      neo_js_context_create_cfunction(ctx, L"throw", neo_js_generator_throw),
      true, false, true);
}

static void neo_js_context_init_std_async_generator(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.async_generator_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_variable_t to_string_tag = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"toStringTag"));

  neo_js_context_set_field(
      ctx, prototype, to_string_tag,
      neo_js_context_create_string(ctx, L"AsyncGenerator"));

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"next"),
                           neo_js_context_create_cfunction(
                               ctx, L"next", neo_js_async_generator_next),
                           true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"return"),
                           neo_js_context_create_cfunction(
                               ctx, L"return", neo_js_async_generator_return),
                           true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"throw"),
                           neo_js_context_create_cfunction(
                               ctx, L"throw", neo_js_async_generator_throw),
                           true, false, true);
}

static void neo_js_context_init_std_array_iterator(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.array_iterator_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_variable_t to_string_tag = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"toStringTag"));

  neo_js_context_set_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, L"ArrayIterator"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"next"),
      neo_js_context_create_cfunction(ctx, L"next", neo_js_array_iterator_next),
      true, false, true);
}
static void neo_js_context_init_std_error(neo_js_context_t ctx) {
  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.error_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString", neo_js_error_to_string),
      true, false, true);
}

static void neo_js_context_init_std_promise(neo_js_context_t ctx) {

  neo_js_context_def_field(
      ctx, ctx->std.promise_constructor,
      neo_js_context_create_string(ctx, L"resolve"),
      neo_js_context_create_cfunction(ctx, L"resolve", neo_js_promise_resolve),
      true, false, true);

  neo_js_context_def_field(
      ctx, ctx->std.promise_constructor,
      neo_js_context_create_string(ctx, L"reject"),
      neo_js_context_create_cfunction(ctx, L"reject", neo_js_promise_reject),
      true, false, true);

  neo_js_variable_t prototype =
      neo_js_context_get_field(ctx, ctx->std.promise_constructor,
                               neo_js_context_create_string(ctx, L"prototype"));

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"then"),
      neo_js_context_create_cfunction(ctx, L"then", neo_js_promise_then), true,
      false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"catch"),
      neo_js_context_create_cfunction(ctx, L"catch", neo_js_promise_catch),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"finally"),
      neo_js_context_create_cfunction(ctx, L"finally", neo_js_promise_finally),
      true, false, true);
}

static void neo_js_context_init_std(neo_js_context_t ctx) {
  ctx->std.object_constructor = neo_js_context_create_cfunction(
      ctx, L"Object", &neo_js_object_constructor);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t prototype =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx), NULL);

  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, L"prototype"),
                           prototype, true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"constructor"),
                           ctx->std.object_constructor, true, false, true);

  neo_js_context_pop_scope(ctx);
  ctx->std.function_constructor = neo_js_context_create_cfunction(
      ctx, L"Function", &neo_js_function_constructor);

  neo_js_context_push_scope(ctx);

  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, L"constructor"),
                           ctx->std.function_constructor, true, false, true);

  neo_js_context_def_field(ctx, ctx->std.function_constructor,
                           neo_js_context_create_string(ctx, L"prototype"),
                           neo_js_context_create_object(ctx, NULL, NULL), true,
                           false, true);

  neo_js_context_pop_scope(ctx);
  ctx->std.symbol_constructor = neo_js_context_create_cfunction(
      ctx, L"Symbol", neo_js_symbol_constructor);

  neo_js_context_push_scope(ctx);

  neo_js_context_init_std_symbol(ctx);

  neo_js_context_init_std_object(ctx);

  neo_js_context_init_std_function(ctx);

  neo_js_context_pop_scope(ctx);

  ctx->std.error_constructor =
      neo_js_context_create_cfunction(ctx, L"Error", neo_js_error_constructor);

  ctx->std.range_error_constructor = neo_js_context_create_cfunction(
      ctx, L"RangeError", neo_js_range_error_constructor);
  neo_js_context_extends(ctx, ctx->std.range_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.type_error_constructor = neo_js_context_create_cfunction(
      ctx, L"TypeError", neo_js_type_error_constructor);
  neo_js_context_extends(ctx, ctx->std.type_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.syntax_error_constructor = neo_js_context_create_cfunction(
      ctx, L"SyntaxError", neo_js_syntax_error_constructor);
  neo_js_context_extends(ctx, ctx->std.syntax_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.reference_error_constructor = neo_js_context_create_cfunction(
      ctx, L"ReferenceError", neo_js_reference_error_constructor);
  neo_js_context_extends(ctx, ctx->std.reference_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.array_constructor =
      neo_js_context_create_cfunction(ctx, L"Array", neo_js_array_constructor);

  ctx->std.array_iterator_constructor = neo_js_context_create_cfunction(
      ctx, L"ArrayIterator", neo_js_array_iterator_constructor);

  ctx->std.generator_function_constructor = neo_js_context_create_cfunction(
      ctx, L"GeneratorFunction", neo_js_generator_function_constructor);

  ctx->std.async_generator_function_constructor =
      neo_js_context_create_cfunction(
          ctx, L"AsyncGeneratorFunction",
          neo_js_async_generator_function_constructor);

  ctx->std.async_function_constructor = neo_js_context_create_cfunction(
      ctx, L"AsyncFunction", neo_js_async_function_constructor);

  ctx->std.generator_constructor = neo_js_context_create_cfunction(
      ctx, L"Generator", neo_js_generator_constructor);

  ctx->std.async_generator_constructor = neo_js_context_create_cfunction(
      ctx, L"AsyncGenerator", neo_js_async_generator_constructor);

  ctx->std.promise_constructor = neo_js_context_create_cfunction(
      ctx, L"Promise", neo_js_promise_constructor);

  ctx->std.global = neo_js_context_create_object(ctx, NULL, NULL);

  neo_js_context_push_scope(ctx);

  neo_js_context_init_std_error(ctx);

  neo_js_context_init_std_array(ctx);

  neo_js_context_init_std_array_iterator(ctx);

  neo_js_context_init_std_generator_function(ctx);

  neo_js_context_init_std_async_generator_function(ctx);

  neo_js_context_init_std_async_function(ctx);

  neo_js_context_init_std_generator(ctx);

  neo_js_context_init_std_async_generator(ctx);

  neo_js_context_init_std_promise(ctx);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"global"),
                           ctx->std.global);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Error"),
                           ctx->std.error_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"RangeError"),
                           ctx->std.range_error_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"TypeError"),
                           ctx->std.type_error_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"SyntaxError"),
                           ctx->std.syntax_error_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"ReferenceError"),
                           ctx->std.reference_error_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Function"),
                           ctx->std.function_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Object"),
                           ctx->std.object_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Symbol"),
                           ctx->std.symbol_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Array"),
                           ctx->std.array_constructor);

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Promise"),
                           ctx->std.promise_constructor);
  neo_js_context_pop_scope(ctx);
}

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t ctx) {
  for (neo_list_node_t it = neo_list_get_first(ctx->coroutines);
       it != neo_list_get_tail(ctx->coroutines); it = neo_list_node_next(it)) {
    neo_js_coroutine_t coroutine = neo_list_node_get(it);
    neo_allocator_free(allocator, coroutine->root);
  }
  neo_allocator_free(allocator, ctx->coroutines);
  for (neo_list_node_t it = neo_list_get_first(ctx->micro_tasks);
       it != neo_list_get_tail(ctx->micro_tasks); it = neo_list_node_next(it)) {
    neo_js_task_t task = neo_list_node_get(it);
    neo_allocator_free(allocator, task);
  }
  neo_allocator_free(allocator, ctx->micro_tasks);
  for (neo_list_node_t it = neo_list_get_first(ctx->macro_tasks);
       it != neo_list_get_tail(ctx->macro_tasks); it = neo_list_node_next(it)) {
    neo_js_task_t task = neo_list_node_get(it);
    neo_allocator_free(allocator, task);
  }
  neo_allocator_free(allocator, ctx->macro_tasks);
  neo_allocator_free(allocator, ctx->task);
  neo_allocator_free(allocator, ctx->root);
  ctx->scope = NULL;
  ctx->task = NULL;
  ctx->root = NULL;
  ctx->runtime = NULL;
  memset(&ctx->std, 0, sizeof(ctx->std));
  neo_allocator_free(allocator, ctx->stacktrace);
}

neo_js_context_t neo_create_js_context(neo_allocator_t allocator,
                                       neo_js_runtime_t runtime) {
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root = neo_create_js_scope(allocator, NULL);
  ctx->task = neo_create_js_scope(allocator, ctx->root);
  ctx->scope = ctx->root;
  memset(&ctx->std, 0, sizeof(ctx->std));
  neo_list_initialize_t initialize = {true};
  ctx->stacktrace = neo_create_list(allocator, &initialize);
  ctx->macro_tasks = neo_create_list(allocator, NULL);
  ctx->micro_tasks = neo_create_list(allocator, NULL);
  ctx->coroutines = neo_create_list(allocator, NULL);
  neo_js_stackframe_t frame = neo_create_js_stackframe(allocator);
  frame->function = neo_create_wstring(allocator, L"start");
  neo_list_push(ctx->stacktrace, frame);
  neo_js_context_push_stackframe(ctx, NULL, L"", 0, 0);
  neo_js_context_init_std(ctx);
  return ctx;
}

neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t ctx) {
  return ctx->runtime;
}

void neo_js_context_next_tick(neo_js_context_t ctx) {
  uint64_t now = neo_clock_get_timestamp();
  neo_list_t tasks = NULL;
  if (neo_list_get_size(ctx->micro_tasks)) {
    tasks = ctx->micro_tasks;
  } else if (neo_list_get_size(ctx->macro_tasks)) {
    tasks = ctx->macro_tasks;
  }
  if (!tasks) {
    return;
  }
  neo_js_task_t task = neo_list_node_get(neo_list_get_first(tasks));
  neo_list_erase(tasks, neo_list_get_first(tasks));
  if (now - task->start < task->timeout) {
    neo_list_push(tasks, task);
    return;
  }
  if (task->keepalive) {
    task->start = now;
    neo_list_push(tasks, task);
  }
  neo_js_context_push_scope(ctx);
  neo_js_variable_t function =
      neo_js_context_create_variable(ctx, task->function, NULL);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t *argv = NULL;
  if (task->argc) {
    argv = neo_allocator_alloc(allocator,
                               sizeof(neo_js_variable_t) * task->argc, NULL);
    for (size_t idx = 0; idx < task->argc; idx++) {
      argv[idx] = neo_js_context_create_variable(ctx, task->argv[idx], NULL);
    }
  }
  neo_js_variable_t error = neo_js_context_call(
      ctx, function, neo_js_context_create_variable(ctx, task->self, NULL),
      task->argc, argv);
  neo_allocator_free(allocator, argv);
  if (!task->keepalive) {
    neo_js_handle_remove_parent(task->function,
                                neo_js_scope_get_root_handle(ctx->task));
  }
  if (neo_js_variable_get_type(error)->kind == NEO_TYPE_ERROR) {
    error = neo_js_error_get_error(ctx, error);
    error = neo_js_context_to_string(ctx, error);
    neo_js_string_t serror = neo_js_variable_to_string(error);
    fprintf(stderr, "Uncaught %ls\n", serror->string);
  }
  neo_js_context_pop_scope(ctx);
  if (!task->keepalive) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_allocator_free(allocator, task->argv);
    neo_allocator_free(allocator, task);
  }
}

bool neo_js_context_is_ready(neo_js_context_t ctx) {
  return neo_list_get_size(ctx->micro_tasks) == 0 &&
         neo_list_get_size(ctx->macro_tasks) == 0;
}

uint32_t neo_js_context_create_micro_task(neo_js_context_t ctx,
                                          neo_js_variable_t callable,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv,
                                          uint64_t timeout, bool keepalive) {
  static uint32_t id = 0;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_task_t task =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_task_t), NULL);
  neo_list_push(ctx->micro_tasks, task);
  task->id = id;
  id++;
  task->timeout = timeout;
  task->keepalive = keepalive;
  task->start = neo_clock_get_timestamp();
  task->function = neo_js_variable_get_handle(callable);
  task->self = neo_js_variable_get_handle(self);
  neo_js_handle_add_parent(task->function,
                           neo_js_scope_get_root_handle(ctx->task));
  neo_js_handle_add_parent(task->self, task->function);
  task->argc = argc;
  if (argc) {
    task->argv =
        neo_allocator_alloc(allocator, sizeof(neo_js_handle_t) * argc, NULL);
    for (uint32_t idx = 0; idx < argc; idx++) {
      task->argv[idx] = neo_js_variable_get_handle(argv[idx]);
      neo_js_handle_add_parent(task->argv[idx], task->function);
    }
  } else {
    task->argv = NULL;
  }
  return task->id;
}

uint32_t neo_js_context_create_macro_task(neo_js_context_t ctx,
                                          neo_js_variable_t callable,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv,
                                          uint64_t timeout, bool keepalive) {
  static uint32_t id = 0;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_task_t task =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_task_t), NULL);
  neo_list_push(ctx->macro_tasks, task);
  task->id = id;
  id++;
  task->timeout = timeout;
  task->keepalive = keepalive;
  task->start = neo_clock_get_timestamp();
  task->function = neo_js_variable_get_handle(callable);
  task->self = neo_js_variable_get_handle(self);
  neo_js_handle_add_parent(task->function,
                           neo_js_scope_get_root_handle(ctx->task));
  neo_js_handle_add_parent(task->self, task->function);
  task->argc = argc;
  if (argc) {
    task->argv =
        neo_allocator_alloc(allocator, sizeof(neo_js_handle_t) * argc, NULL);
    for (uint32_t idx = 0; idx < argc; idx++) {
      task->argv[idx] = neo_js_variable_get_handle(argv[idx]);
      neo_js_handle_add_parent(task->argv[idx], task->function);
    }
  } else {
    task->argv = NULL;
  }
  return task->id;
}

void neo_js_context_kill_micro_task(neo_js_context_t ctx, uint32_t id) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_handle_t hroot = neo_js_scope_get_root_handle(ctx->task);
  for (neo_list_node_t it = neo_list_get_first(ctx->micro_tasks);
       it != neo_list_get_tail(ctx->micro_tasks); it = neo_list_node_next(it)) {
    neo_js_task_t task = neo_list_node_get(it);
    if (task->id == id) {
      neo_js_context_push_scope(ctx);
      neo_js_handle_add_parent(task->function,
                               neo_js_scope_get_root_handle(ctx->scope));
      neo_js_handle_remove_parent(task->function, hroot);
      neo_allocator_free(allocator, task->argv);
      neo_allocator_free(allocator, task);
      neo_list_erase(ctx->micro_tasks, it);
      neo_js_context_pop_scope(ctx);
      return;
    }
  }
}

void neo_js_context_kill_macro_task(neo_js_context_t ctx, uint32_t id) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_handle_t hroot = neo_js_scope_get_root_handle(ctx->task);
  for (neo_list_node_t it = neo_list_get_first(ctx->macro_tasks);
       it != neo_list_get_tail(ctx->macro_tasks); it = neo_list_node_next(it)) {
    neo_js_task_t task = neo_list_node_get(it);
    if (task->id == id) {
      neo_js_context_push_scope(ctx);
      neo_js_handle_add_parent(task->function,
                               neo_js_scope_get_root_handle(ctx->scope));
      neo_js_handle_remove_parent(task->function, hroot);
      neo_allocator_free(allocator, task->argv);
      neo_allocator_free(allocator, task);
      neo_list_erase(ctx->macro_tasks, it);
      neo_js_context_pop_scope(ctx);
      return;
    }
  }
}

neo_js_scope_t neo_js_context_get_scope(neo_js_context_t ctx) {
  return ctx->scope;
}

neo_js_scope_t neo_js_context_set_scope(neo_js_context_t ctx,
                                        neo_js_scope_t scope) {
  neo_js_scope_t current = ctx->scope;
  ctx->scope = scope;
  return current;
}

neo_js_scope_t neo_js_context_get_root(neo_js_context_t ctx) {
  return ctx->root;
}

neo_js_variable_t neo_js_context_get_global(neo_js_context_t ctx) {
  return ctx->std.global;
}

neo_js_scope_t neo_js_context_push_scope(neo_js_context_t ctx) {
  ctx->scope = neo_create_js_scope(neo_js_runtime_get_allocator(ctx->runtime),
                                   ctx->scope);
  return ctx->scope;
}

neo_js_scope_t neo_js_context_pop_scope(neo_js_context_t ctx) {
  neo_js_scope_t current = ctx->scope;
  ctx->scope = neo_js_scope_get_parent(ctx->scope);
  neo_allocator_free(neo_js_runtime_get_allocator(ctx->runtime), current);
  return ctx->scope;
}

neo_list_t neo_js_context_get_stacktrace(neo_js_context_t ctx, uint32_t line,
                                         uint32_t column) {
  neo_list_node_t it = neo_list_get_last(ctx->stacktrace);
  neo_js_stackframe_t frame = neo_list_node_get(it);

  frame->column = column;
  frame->line = line;
  return ctx->stacktrace;
}

void neo_js_context_push_stackframe(neo_js_context_t ctx,
                                    const wchar_t *filename,
                                    const wchar_t *function, uint32_t column,
                                    uint32_t line) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_list_node_t it = neo_list_get_last(ctx->stacktrace);
  neo_js_stackframe_t frame = neo_list_node_get(it);

  frame->column = column;
  frame->line = line;
  if (filename) {
    frame->filename = neo_create_wstring(allocator, filename);
  } else {
    frame->filename = NULL;
  }
  frame = neo_create_js_stackframe(allocator);
  if (function) {
    frame->function = neo_create_wstring(allocator, function);
  } else {
    frame->function = neo_create_wstring(allocator, L"anonymous");
  }
  neo_list_push(ctx->stacktrace, frame);
}

void neo_js_context_pop_stackframe(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_list_pop(ctx->stacktrace);
  neo_list_node_t it = neo_list_get_last(ctx->stacktrace);
  neo_js_stackframe_t frame = neo_list_node_get(it);
  frame->line = 0;
  frame->column = 0;
  neo_allocator_free(allocator, frame->filename);
}

neo_js_variable_t neo_js_context_get_error_constructor(neo_js_context_t ctx) {
  return ctx->std.error_constructor;
}

neo_js_variable_t
neo_js_context_get_type_error_constructor(neo_js_context_t ctx) {
  return ctx->std.type_error_constructor;
}

neo_js_variable_t
neo_js_context_get_range_error_constructor(neo_js_context_t ctx) {
  return ctx->std.range_error_constructor;
}

neo_js_variable_t
neo_js_context_get_reference_error_constructor(neo_js_context_t ctx) {
  return ctx->std.reference_error_constructor;
}
neo_js_variable_t
neo_js_context_get_syntax_error_constructor(neo_js_context_t ctx) {
  return ctx->std.syntax_error_constructor;
}

neo_js_variable_t neo_js_context_get_object_constructor(neo_js_context_t ctx) {
  return ctx->std.object_constructor;
}

neo_js_variable_t
neo_js_context_get_function_constructor(neo_js_context_t ctx) {
  return ctx->std.function_constructor;
}

neo_js_variable_t neo_js_context_get_string_constructor(neo_js_context_t ctx) {
  return ctx->std.string_constructor;
}

neo_js_variable_t neo_js_context_get_boolean_constructor(neo_js_context_t ctx) {
  return ctx->std.boolean_constructor;
}

neo_js_variable_t neo_js_context_get_number_constructor(neo_js_context_t ctx) {
  return ctx->std.number_constructor;
}

neo_js_variable_t neo_js_context_get_symbol_constructor(neo_js_context_t ctx) {
  return ctx->std.symbol_constructor;
}

neo_js_variable_t neo_js_context_get_array_constructor(neo_js_context_t ctx) {
  return ctx->std.array_constructor;
}

neo_js_variable_t
neo_js_context_get_array_iterator_constructor(neo_js_context_t ctx) {
  return ctx->std.array_iterator_constructor;
}

neo_js_variable_t neo_js_context_get_promise_constructor(neo_js_context_t ctx) {
  return ctx->std.promise_constructor;
}

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t ctx,
                                                 neo_js_handle_t handle,
                                                 const wchar_t *name) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  return neo_js_scope_create_variable(allocator, ctx->scope, handle, NULL);
}

neo_js_variable_t neo_js_context_def_variable(neo_js_context_t ctx,
                                              neo_js_variable_t variable,
                                              const wchar_t *name) {
  neo_js_variable_t current = neo_js_scope_get_variable(ctx->scope, name);
  if (current != NULL) {
    wchar_t msg[1024];
    swprintf(msg, 1024, L"cannot redefine variable: '%ls'", name);
    return neo_js_context_create_error(ctx, NEO_ERROR_SYNTAX, msg);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_handle_t handle = neo_js_variable_get_handle(variable);
  neo_js_scope_create_variable(allocator, ctx->scope, handle, name);
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_store_variable(neo_js_context_t ctx,
                                                neo_js_variable_t variable,
                                                const wchar_t *name) {
  neo_js_variable_t current = neo_js_context_load_variable(ctx, name);
  if (neo_js_variable_get_type(current)->kind == NEO_TYPE_ERROR) {
    return current;
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_TYPE_ERROR) {
    return variable;
  }
  neo_js_type_t type = neo_js_variable_get_type(variable);
  neo_js_context_push_scope(ctx);
  type->copy_fn(ctx, variable, current);
  neo_js_context_pop_scope(ctx);
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_load_variable(neo_js_context_t ctx,
                                               const wchar_t *name) {
  neo_js_scope_t scope = ctx->scope;
  while (scope) {
    neo_js_variable_t variable = neo_js_scope_get_variable(scope, name);
    if (variable) {
      return neo_js_context_create_variable(
          ctx, neo_js_variable_get_handle(variable), NULL);
    }
    scope = neo_js_scope_get_parent(scope);
  }
  neo_js_variable_t field = neo_js_context_create_string(ctx, name);
  if (neo_js_context_has_field(ctx, ctx->std.global, field)) {
    return neo_js_context_get_field(ctx, ctx->std.global, field);
  }
  wchar_t msg[1024];
  swprintf(msg, 1024, L"variable '%ls' is not defined", name);
  return neo_js_context_create_error(ctx, NEO_ERROR_REFERENCE, msg);
}

neo_js_variable_t neo_js_context_extends(neo_js_context_t ctx,
                                         neo_js_variable_t variable,
                                         neo_js_variable_t parent) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, variable, neo_js_context_create_string(ctx, L"prototype"));

  neo_js_variable_t parent_prototype = neo_js_context_get_field(
      ctx, parent, neo_js_context_create_string(ctx, L"prototype"));

  return neo_js_object_set_prototype(ctx, prototype, parent_prototype);
}

neo_js_variable_t neo_js_context_to_primitive(neo_js_context_t ctx,
                                              neo_js_variable_t variable,
                                              const wchar_t *hint) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_value_t value = neo_js_variable_get_value(variable);
  neo_js_variable_t result = value->type->to_primitive_fn(ctx, variable, hint);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_object(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_value_t value = neo_js_variable_get_value(variable);
  neo_js_variable_t result = value->type->to_object_fn(ctx, variable);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->get_field_fn(ctx, object, field);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_set_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field,
                                           neo_js_variable_t value) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->set_field_fn(ctx, object, field, value);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

bool neo_js_context_has_field(neo_js_context_t ctx, neo_js_variable_t object,
                              neo_js_variable_t field) {
  object = neo_js_context_to_object(ctx, object);
  return neo_js_object_get_property(ctx, object, field) != NULL;
}

neo_js_variable_t
neo_js_context_def_field(neo_js_context_t ctx, neo_js_variable_t object,
                         neo_js_variable_t field, neo_js_variable_t value,
                         bool configurable, bool enumable, bool writable) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (neo_js_variable_get_type(object)->kind < NEO_TYPE_OBJECT) {
    return neo_js_context_create_error(
        ctx, NEO_ERROR_TYPE, L"Object.defineProperty called on non-object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  field = neo_js_context_to_primitive(ctx, field, L"default");
  if (neo_js_variable_get_type(field)->kind != NEO_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(ctx, object, field);
  neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  if (!prop) {
    if (obj->sealed || obj->frozen || !obj->extensible) {
      return neo_js_context_create_error(ctx, NEO_ERROR_TYPE,
                                         L"Object is not extensible");
    }
    prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumerable = enumable;
    prop->writable = writable;
    prop->value = hvalue;
    prop->get = NULL;
    prop->set = NULL;
    neo_js_handle_t hfield = neo_js_variable_get_handle(field);
    neo_hash_map_set(obj->properties, hfield, prop, ctx, ctx);
    neo_js_handle_add_parent(hvalue, hobject);
    neo_js_handle_add_parent(hfield, hobject);
    if (neo_js_variable_get_type(field)->kind == NEO_TYPE_STRING &&
        prop->enumerable) {
      neo_list_push(obj->keys, hfield);
    }
    if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
      neo_list_push(obj->symbol_keys, hfield);
    }
  } else {
    if (obj->frozen) {
      return neo_js_context_create_error(ctx, NEO_ERROR_TYPE,
                                         L"Object is not extensible");
    }
    if (prop->value != NULL) {
      neo_js_variable_t current =
          neo_js_context_create_variable(ctx, prop->value, NULL);
      if (neo_js_variable_get_type(current) ==
              neo_js_variable_get_type(value) &&
          neo_js_context_is_equal(ctx, current, value)) {
        return object;
      }
      if (!prop->configurable || !prop->writable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
          neo_js_symbol_t sym = neo_js_variable_to_symbol(field);
          size_t len = wcslen(sym->description) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property Symbol(%ls) of object "
                   L"'#<Object>'",
                   sym->description);
        } else {
          neo_js_string_t str = neo_js_variable_to_string(field);
          size_t len = wcslen(str->string) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property '%ls' of object "
                   L"'#<Object>'",
                   str->string);
        }
        neo_js_variable_t error =
            neo_js_context_create_error(ctx, NEO_ERROR_TYPE, message);
        neo_allocator_free(allocator, message);
        return error;
      }
      neo_js_handle_remove_parent(prop->value, hobject);
      neo_js_handle_add_parent(prop->value,
                               neo_js_scope_get_root_handle(ctx->scope));
    } else {
      if (!prop->configurable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
          neo_js_symbol_t sym = neo_js_variable_to_symbol(field);
          size_t len = wcslen(sym->description) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len, L"Cannot redefine property: Symbol(%ls)",
                   sym->description);
        } else {
          neo_js_string_t str = neo_js_variable_to_string(field);
          size_t len = wcslen(str->string) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len, L"Cannot redefine property: '%ls'",
                   str->string);
        }
        neo_js_variable_t error =
            neo_js_context_create_error(ctx, NEO_ERROR_TYPE, message);
        neo_allocator_free(allocator, message);
        return error;
      }
      if (prop->get) {
        neo_js_handle_add_parent(prop->get,
                                 neo_js_scope_get_root_handle(ctx->scope));
        neo_js_handle_remove_parent(prop->get, hobject);
        prop->get = NULL;
      }
      if (prop->set) {
        neo_js_handle_add_parent(prop->set,
                                 neo_js_scope_get_root_handle(ctx->scope));
        neo_js_handle_remove_parent(prop->set, hobject);
        prop->set = NULL;
      }
    }
    neo_js_handle_add_parent(hvalue, hobject);
    prop->value = hvalue;
    prop->writable = writable;
  }
  return object;
}

neo_js_variable_t
neo_js_context_def_accessor(neo_js_context_t ctx, neo_js_variable_t object,
                            neo_js_variable_t field, neo_js_variable_t getter,
                            neo_js_variable_t setter, bool configurable,
                            bool enumable) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (neo_js_variable_get_type(object)->kind < NEO_TYPE_OBJECT) {
    return neo_js_context_create_error(
        ctx, NEO_ERROR_TYPE, L"Object.defineProperty called on non-object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  field = neo_js_context_to_primitive(ctx, field, L"default");
  if (neo_js_variable_get_type(field)->kind != NEO_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(ctx, object, field);
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  neo_js_handle_t hgetter = NULL;
  neo_js_handle_t hsetter = NULL;
  if (getter) {
    hgetter = neo_js_variable_get_handle(getter);
  }
  if (setter) {
    hsetter = neo_js_variable_get_handle(setter);
  }
  if (!prop) {
    if (obj->sealed || obj->frozen || !obj->extensible) {
      return neo_js_context_create_error(ctx, NEO_ERROR_TYPE,
                                         L"Object is not extensible");
    }
    prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumerable = enumable;
    prop->get = hgetter;
    prop->set = hsetter;
    if (hgetter) {
      neo_js_handle_add_parent(hgetter, hobject);
    }
    if (hsetter) {
      neo_js_handle_add_parent(hsetter, hobject);
    }
  } else {
    if (prop->value != NULL) {
      if (!prop->configurable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
          neo_js_symbol_t sym = neo_js_variable_to_symbol(field);
          size_t len = wcslen(sym->description) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property Symbol(%ls) of object "
                   L"'#<Object>'",
                   sym->description);
        } else {
          neo_js_string_t str = neo_js_variable_to_string(field);
          size_t len = wcslen(str->string) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len,
                   L"Cannot assign to read only property '%ls' of object "
                   L"'#<Object>'",
                   str->string);
        }
        neo_js_variable_t error =
            neo_js_context_create_error(ctx, NEO_ERROR_TYPE, message);
        neo_allocator_free(allocator, message);
        return error;
      }
      neo_js_handle_add_parent(prop->value,
                               neo_js_scope_get_root_handle(ctx->scope));
      neo_js_handle_remove_parent(prop->value, hobject);
      prop->value = NULL;
      prop->writable = false;
    } else {
      if (!prop->configurable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
          neo_js_symbol_t sym = neo_js_variable_to_symbol(field);
          size_t len = wcslen(sym->description) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len, L"Cannot redefine property: Symbol(%ls)",
                   sym->description);
        } else {
          neo_js_string_t str = neo_js_variable_to_string(field);
          size_t len = wcslen(str->string) + 64;
          message = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          swprintf(message, len, L"Cannot redefine property: '%ls'",
                   str->string);
        }
        neo_js_variable_t error =
            neo_js_context_create_error(ctx, NEO_ERROR_TYPE, message);
        neo_allocator_free(allocator, message);
        return error;
      }
    }
    if (prop->get) {
      neo_js_handle_add_parent(prop->get,
                               neo_js_scope_get_root_handle(ctx->scope));
      neo_js_handle_remove_parent(prop->get, hobject);
      prop->get = NULL;
    }
    if (prop->set) {
      neo_js_handle_add_parent(prop->set,
                               neo_js_scope_get_root_handle(ctx->scope));
      neo_js_handle_remove_parent(prop->set, hobject);
      prop->set = NULL;
    }
    if (hgetter) {
      neo_js_handle_add_parent(hgetter, hobject);
      prop->get = hgetter;
    }
    if (hsetter) {
      neo_js_handle_add_parent(hsetter, hobject);
      prop->set = hsetter;
    }
  }
  return object;
}

neo_js_variable_t neo_js_context_get_internal(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              const wchar_t *field) {
  neo_js_object_t object = neo_js_variable_to_object(self);
  if (object) {
    return neo_js_context_create_variable(
        ctx, neo_hash_map_get(object->internal, field, NULL, NULL), NULL);
  }
  return neo_js_context_create_undefined(ctx);
}

void neo_js_context_set_internal(neo_js_context_t ctx, neo_js_variable_t self,
                                 const wchar_t *field,
                                 neo_js_variable_t value) {
  neo_js_object_t object = neo_js_variable_to_object(self);
  if (object) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_js_handle_t current =
        neo_hash_map_get(object->internal, field, NULL, NULL);
    if (current) {
      neo_js_handle_add_parent(current,
                               neo_js_scope_get_root_handle(ctx->scope));
    }
    neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
    neo_hash_map_set(object->internal, neo_create_wstring(allocator, field),
                     hvalue, NULL, NULL);
    neo_js_handle_add_parent(hvalue, neo_js_variable_get_handle(self));
  }
}

void *neo_js_context_get_opaque(neo_js_context_t ctx,
                                neo_js_variable_t variable,
                                const wchar_t *key) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return neo_hash_map_get(value->opaque, key, NULL, NULL);
}

void neo_js_context_set_opaque(neo_js_context_t ctx, neo_js_variable_t variable,
                               const wchar_t *field, void *value) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  size_t len = wcslen(field);
  wchar_t *key =
      neo_allocator_alloc(allocator, sizeof(wchar_t) * (len + 1), NULL);
  wcscpy(key, field);
  neo_js_value_t val = neo_js_variable_get_value(variable);
  neo_hash_map_set(val->opaque, key, value, NULL, NULL);
}

neo_js_variable_t neo_js_context_del_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->del_field_fn(ctx, object, field);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_keys(neo_js_context_t ctx,
                                          neo_js_variable_t variable) {
  variable = neo_js_context_to_object(ctx, variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_TYPE_ERROR) {
    return variable;
  }
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_list_t keys = neo_js_object_get_keys(ctx, variable);
  neo_js_variable_t result =
      neo_js_context_create_array(ctx, neo_list_get_size(keys));
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(keys);
       it != neo_list_get_tail(keys); it = neo_list_node_next(it)) {
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_number(ctx, idx),
                             neo_list_node_get(it));
    idx++;
  }
  neo_allocator_free(allocator, keys);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_clone(neo_js_context_t ctx,
                                       neo_js_variable_t self) {
  neo_js_variable_t variable = neo_js_context_create_undefined(ctx);
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(self);
  type->copy_fn(ctx, self, variable);
  neo_js_context_pop_scope(ctx);
  return variable;
}

neo_js_variable_t neo_js_context_assigment(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           neo_js_variable_t target) {
  if (neo_js_variable_is_const(target)) {
    return neo_js_context_create_error(ctx, NEO_ERROR_TYPE,
                                       L"Assignment to constant variable.");
  }
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(self);
  type->copy_fn(ctx, self, target);
  neo_js_context_pop_scope(ctx);
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t
neo_js_context_call_cfunction(neo_js_context_t ctx, neo_js_variable_t callee,
                              neo_js_variable_t self, uint32_t argc,
                              neo_js_variable_t *argv) {
  neo_js_callable_t callable = neo_js_variable_to_callable(callee);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
  neo_js_scope_t current = neo_js_context_set_scope(ctx, scope);
  if (callable->bind) {
    self = neo_js_context_create_variable(ctx, callable->bind, NULL);
  } else {
    self = neo_js_scope_create_variable(allocator, scope,
                                        neo_js_variable_get_handle(self), NULL);
  }
  neo_js_variable_t result = neo_js_context_create_undefined(ctx);
  neo_js_cfunction_t cfunction = neo_js_variable_to_cfunction(callee);
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t arg = argv[idx];
    neo_js_handle_t harg = neo_js_variable_get_handle(arg);
    neo_js_handle_add_parent(harg, neo_js_scope_get_root_handle(scope));
  }
  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(cfunction->callable.closure);
       it != neo_hash_map_get_tail(cfunction->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const wchar_t *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_variable(neo_js_runtime_get_allocator(ctx->runtime),
                                 scope, hvalue, name);
  }
  result = cfunction->callee(ctx, self, argc, argv);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_set_scope(ctx, current);
  neo_allocator_free(allocator, scope);
  return result;
}

static neo_js_variable_t neo_js_context_call_generator_function(
    neo_js_context_t ctx, neo_js_variable_t callee, neo_js_variable_t self,
    uint32_t argc, neo_js_variable_t *argv) {
  neo_js_function_t function = neo_js_variable_to_function(callee);
  neo_js_variable_t result =
      neo_js_context_construct(ctx, ctx->std.generator_constructor, 0, NULL);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
  neo_js_vm_t vm = neo_create_js_vm(ctx, self, function->address);

  neo_js_coroutine_t coroutine = neo_js_context_create_coroutine(ctx);

  coroutine->program = function->program;
  coroutine->vm = vm;
  coroutine->root = scope;
  coroutine->scope = coroutine->root;

  neo_js_context_set_opaque(ctx, result, L"coroutine", coroutine);

  neo_js_scope_t current = neo_js_context_set_scope(ctx, scope);
  neo_js_handle_t bind = function->callable.bind;
  if (!function->callable.bind) {
    bind = neo_js_variable_get_handle(self);
  }
  self = neo_js_context_create_variable(ctx, bind, NULL);
  neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL, NULL);
  neo_js_scope_set_variable(allocator, scope, arguments, L"arguments");
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_context_set_field(ctx, arguments,
                             neo_js_context_create_number(ctx, idx), argv[idx]);
  }
  neo_js_context_set_field(ctx, arguments,
                           neo_js_context_create_string(ctx, L"length"),
                           neo_js_context_create_number(ctx, argc));

  neo_js_context_set_field(
      ctx, arguments,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"iterator")),
      neo_js_context_create_cfunction(ctx, L"values", neo_js_array_values));

  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const wchar_t *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_variable(neo_js_runtime_get_allocator(ctx->runtime),
                                 scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  return result;
}

bool neo_js_context_is_thenable(neo_js_context_t ctx,
                                neo_js_variable_t variable) {
  if (neo_js_variable_get_type(variable)->kind < NEO_TYPE_OBJECT) {
    return false;
  }
  neo_js_variable_t then = neo_js_context_get_field(
      ctx, variable, neo_js_context_create_string(ctx, L"then"));
  if (neo_js_variable_get_type(then)->kind < NEO_TYPE_CALLABLE) {
    return false;
  }
  return true;
}

neo_js_coroutine_t neo_js_context_create_coroutine(neo_js_context_t ctx) {
  neo_js_coroutine_t coroutine =
      neo_create_js_coroutine(neo_js_runtime_get_allocator(ctx->runtime));
  neo_list_push(ctx->coroutines, coroutine);
  return coroutine;
}
void neo_js_context_recycle_coroutine(neo_js_context_t ctx,
                                      neo_js_coroutine_t coroutine) {
  neo_list_delete(ctx->coroutines, coroutine);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_allocator_free(allocator, coroutine->scope);
}

static neo_js_variable_t neo_js_context_call_async_function(
    neo_js_context_t ctx, neo_js_variable_t callee, neo_js_variable_t self,
    uint32_t argc, neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t neo_js_context_call_async_generator_function(
    neo_js_context_t ctx, neo_js_variable_t callee, neo_js_variable_t self,
    uint32_t argc, neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t neo_js_context_call_function(neo_js_context_t ctx,
                                                      neo_js_variable_t callee,
                                                      neo_js_variable_t self,
                                                      uint32_t argc,
                                                      neo_js_variable_t *argv) {
  neo_js_function_t function = neo_js_variable_to_function(callee);

  if (function->is_generator) {
    if (function->is_async) {
      return neo_js_context_call_async_generator_function(ctx, callee, self,
                                                          argc, argv);
    } else {
      return neo_js_context_call_generator_function(ctx, callee, self, argc,
                                                    argv);
    }
  } else {
    if (function->is_async) {
      return neo_js_context_call_async_function(ctx, callee, self, argc, argv);
    } else {
      neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
      neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
      neo_js_scope_t current = neo_js_context_set_scope(ctx, scope);
      if (function->callable.bind) {
        self =
            neo_js_context_create_variable(ctx, function->callable.bind, NULL);
      } else {
        self = neo_js_scope_create_variable(
            allocator, scope, neo_js_variable_get_handle(self), NULL);
      }
      neo_js_variable_t arguments =
          neo_js_context_create_object(ctx, NULL, NULL);
      neo_js_scope_set_variable(allocator, scope, arguments, L"arguments");
      for (uint32_t idx = 0; idx < argc; idx++) {
        neo_js_context_set_field(
            ctx, arguments, neo_js_context_create_number(ctx, idx), argv[idx]);
      }
      neo_js_context_set_field(ctx, arguments,
                               neo_js_context_create_string(ctx, L"length"),
                               neo_js_context_create_number(ctx, argc));

      neo_js_context_set_field(
          ctx, arguments,
          neo_js_context_get_field(
              ctx, ctx->std.symbol_constructor,
              neo_js_context_create_string(ctx, L"iterator")),
          neo_js_context_create_cfunction(ctx, L"values", neo_js_array_values));
      for (neo_hash_map_node_t it =
               neo_hash_map_get_first(function->callable.closure);
           it != neo_hash_map_get_tail(function->callable.closure);
           it = neo_hash_map_node_next(it)) {
        const wchar_t *name = neo_hash_map_node_get_key(it);
        neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
        neo_js_scope_create_variable(neo_js_runtime_get_allocator(ctx->runtime),
                                     scope, hvalue, name);
      }
      neo_js_vm_t vm = neo_create_js_vm(ctx, self, function->address);
      neo_js_variable_t result = neo_js_vm_eval(vm, function->program);
      neo_allocator_free(neo_js_context_get_allocator(ctx), vm);
      neo_js_handle_t hresult = neo_js_variable_get_handle(result);
      result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
      while (neo_js_context_get_scope(ctx) != scope) {
        neo_js_context_pop_scope(ctx);
      }
      neo_js_context_set_scope(ctx, current);
      neo_allocator_free(allocator, scope);
      return result;
    }
  }
}

neo_js_variable_t neo_js_context_call(neo_js_context_t ctx,
                                      neo_js_variable_t callee,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(callee)->kind == NEO_TYPE_CFUNCTION) {
    return neo_js_context_call_cfunction(ctx, callee, self, argc, argv);
  } else if (neo_js_variable_get_type(callee)->kind == NEO_TYPE_FUNCTION) {
    return neo_js_context_call_function(ctx, callee, self, argc, argv);
  } else {
    return neo_js_context_create_error(ctx, NEO_ERROR_TYPE,
                                       L"callee is not a function");
  }
}

neo_js_variable_t neo_js_context_construct(neo_js_context_t ctx,
                                           neo_js_variable_t constructor,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(constructor)->kind != NEO_TYPE_CFUNCTION) {
    return neo_js_context_create_error(ctx, NEO_ERROR_TYPE,
                                       L"Constructor is not a cfunction");
  }
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_scope_t current = neo_js_context_get_scope(ctx);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"prototype"));
  neo_js_object_t obj = NULL;
  if (neo_js_variable_get_value(constructor) ==
      neo_js_variable_get_value(ctx->std.array_constructor)) {
    obj = &neo_create_js_array(allocator)->object;
  } else {
    obj = neo_create_js_object(allocator);
  }
  neo_js_variable_t object = neo_js_context_create_object(ctx, prototype, obj);
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  obj->constructor = neo_js_variable_get_handle(constructor);
  neo_js_handle_add_parent(obj->constructor, hobject);
  neo_js_context_def_field(ctx, object,
                           neo_js_context_create_string(ctx, L"constructor"),
                           constructor, true, false, true);
  neo_js_variable_t result =
      neo_js_context_call(ctx, constructor, object, argc, argv);
  if (result) {
    if (neo_js_variable_get_type(result)->kind >= NEO_TYPE_OBJECT) {
      object = result;
    }
  }
  hobject = neo_js_variable_get_handle(object);
  object = neo_js_scope_create_variable(allocator, current, hobject, NULL);
  neo_js_context_pop_scope(ctx);
  return object;
}

neo_js_variable_t neo_js_context_create_error(neo_js_context_t ctx,
                                              neo_js_error_type_t type,
                                              const wchar_t *message) {
  neo_js_variable_t msg = neo_js_context_create_string(ctx, message);
  neo_js_variable_t value = NULL;
  switch (type) {
  case NEO_ERROR_SYNTAX:
    value = neo_js_context_construct(ctx, ctx->std.syntax_error_constructor, 1,
                                     &msg);
    break;
  case NEO_ERROR_RANGE:
    value = neo_js_context_construct(ctx, ctx->std.range_error_constructor, 1,
                                     &msg);
    break;
  case NEO_ERROR_TYPE:
    value =
        neo_js_context_construct(ctx, ctx->std.type_error_constructor, 1, &msg);
    break;
  case NEO_ERROR_REFERENCE:
    value = neo_js_context_construct(ctx, ctx->std.reference_error_constructor,
                                     1, &msg);
    break;
  }
  return neo_js_context_create_value_error(ctx, value);
}

neo_js_variable_t neo_js_context_create_value_error(neo_js_context_t ctx,
                                                    neo_js_variable_t value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_error_t error = neo_create_js_error(allocator, value);

  neo_js_variable_t variable = neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &error->value), NULL);
  neo_js_handle_t hvariable = neo_js_variable_get_handle(variable);
  neo_js_handle_t herror = neo_js_variable_get_handle(value);
  neo_js_handle_add_parent(herror, hvariable);
  return variable;
}

neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_undefined_t undefined = neo_create_js_undefined(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &undefined->value), NULL);
}
neo_js_variable_t neo_js_context_create_uninitialize(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_uninitialize_t uninitialize = neo_create_js_uninitialize(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &uninitialize->value), NULL);
}
neo_js_variable_t neo_js_context_create_null(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_null_t null = neo_create_js_null(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &null->value), NULL);
}

neo_js_variable_t neo_js_context_create_number(neo_js_context_t ctx,
                                               double value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_number_t number = neo_create_js_number(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &number->value), NULL);
}

neo_js_variable_t neo_js_context_create_string(neo_js_context_t ctx,
                                               const wchar_t *value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_string_t string = neo_create_js_string(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value), NULL);
}

neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t ctx,
                                                bool value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_boolean_t boolean = neo_create_js_boolean(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &boolean->value), NULL);
}

neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t ctx,
                                               const wchar_t *description) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_symbol_t symbol = neo_create_js_symbol(allocator, description);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &symbol->value), NULL);
}
neo_js_variable_t neo_js_context_create_object(neo_js_context_t ctx,
                                               neo_js_variable_t prototype,
                                               neo_js_object_t object) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  if (!object) {
    object = neo_create_js_object(allocator);
  }
  neo_js_handle_t hobject = neo_create_js_handle(allocator, &object->value);
  if (!prototype) {
    if (ctx->std.object_constructor) {
      prototype = neo_js_context_get_field(
          ctx, ctx->std.object_constructor,
          neo_js_context_create_string(ctx, L"prototype"));
    } else {
      prototype = neo_js_context_create_null(ctx);
    }
  }
  neo_js_handle_t hproto = neo_js_variable_get_handle(prototype);
  neo_js_handle_add_parent(hproto, hobject);
  object->prototype = hproto;
  neo_js_variable_t result = neo_js_context_create_variable(ctx, hobject, NULL);
  if (ctx->std.object_constructor) {
    neo_js_context_def_field(ctx, result,
                             neo_js_context_create_string(ctx, L"constructor"),
                             ctx->std.object_constructor, true, false, true);
  }
  return result;
}

neo_js_variable_t neo_js_context_create_array(neo_js_context_t ctx,
                                              uint32_t length) {
  neo_js_variable_t array =
      neo_js_context_construct(ctx, ctx->std.array_constructor, 0, NULL);
  neo_js_context_set_field(ctx, array,
                           neo_js_context_create_string(ctx, L"length"),
                           neo_js_context_create_number(ctx, length));
  return array;
}

neo_js_variable_t
neo_js_context_create_interrupt(neo_js_context_t ctx, neo_js_variable_t result,
                                size_t offset, neo_js_interrupt_type_t type) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  neo_js_interrupt_t interrupt =
      neo_create_js_interrupt(allocator, hresult, offset, type);
  neo_js_variable_t variable = neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &interrupt->value), NULL);
  neo_js_handle_t hvariable = neo_js_variable_get_handle(variable);
  neo_js_handle_add_parent(hresult, hvariable);
  return variable;
}

neo_js_variable_t
neo_js_context_create_cfunction(neo_js_context_t ctx, const wchar_t *name,
                                neo_js_cfunction_fn_t cfunction) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_cfunction_t func = neo_create_js_cfunction(allocator, cfunction);
  neo_js_handle_t hfunction =
      neo_create_js_handle(allocator, &func->callable.object.value);
  neo_js_handle_t hproto = NULL;
  if (ctx->std.function_constructor) {
    hproto = neo_js_variable_get_handle(neo_js_context_get_field(
        ctx, ctx->std.function_constructor,
        neo_js_context_create_string(ctx, L"prototype")));
  } else {
    hproto = neo_js_variable_get_handle(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_handle_add_parent(hproto, hfunction);
  if (name) {
    func->callable.name = neo_create_wstring(allocator, name);
  }
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.function_constructor) {
    neo_js_context_def_field(ctx, result,
                             neo_js_context_create_string(ctx, L"constructor"),
                             ctx->std.function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, L"prototype"),
      neo_js_context_create_object(ctx, NULL, NULL), true, false, true);
  return result;
}
neo_js_variable_t neo_js_context_create_function(neo_js_context_t ctx,
                                                 neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, program);
  neo_js_handle_t hfunction =
      neo_create_js_handle(allocator, &func->callable.object.value);
  neo_js_handle_t hproto = NULL;
  if (ctx->std.function_constructor) {
    hproto = neo_js_variable_get_handle(neo_js_context_get_field(
        ctx, ctx->std.function_constructor,
        neo_js_context_create_string(ctx, L"prototype")));
  } else {
    hproto = neo_js_variable_get_handle(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_handle_add_parent(hproto, hfunction);
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.function_constructor) {
    neo_js_context_def_field(ctx, result,
                             neo_js_context_create_string(ctx, L"constructor"),
                             ctx->std.function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, L"prototype"),
      neo_js_context_create_object(ctx, NULL, NULL), true, false, true);
  return result;
}

neo_js_variable_t
neo_js_context_create_generator_function(neo_js_context_t ctx,
                                         neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, program);
  func->is_generator = true;
  neo_js_handle_t hfunction =
      neo_create_js_handle(allocator, &func->callable.object.value);
  neo_js_handle_t hproto = NULL;
  if (ctx->std.generator_function_constructor) {
    hproto = neo_js_variable_get_handle(neo_js_context_get_field(
        ctx, ctx->std.generator_function_constructor,
        neo_js_context_create_string(ctx, L"prototype")));
  } else {
    hproto = neo_js_variable_get_handle(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_handle_add_parent(hproto, hfunction);
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.generator_function_constructor) {
    neo_js_context_def_field(
        ctx, result, neo_js_context_create_string(ctx, L"constructor"),
        ctx->std.generator_function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, L"prototype"),
      neo_js_context_create_object(ctx, NULL, NULL), true, false, true);
  return result;
}

neo_js_variable_t neo_js_context_create_async_function(neo_js_context_t ctx,
                                                       neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, program);
  func->is_async = true;
  neo_js_handle_t hfunction =
      neo_create_js_handle(allocator, &func->callable.object.value);
  neo_js_handle_t hproto = NULL;
  if (ctx->std.async_function_constructor) {
    hproto = neo_js_variable_get_handle(neo_js_context_get_field(
        ctx, ctx->std.async_function_constructor,
        neo_js_context_create_string(ctx, L"prototype")));
  } else {
    hproto = neo_js_variable_get_handle(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_handle_add_parent(hproto, hfunction);
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.async_function_constructor) {
    neo_js_context_def_field(
        ctx, result, neo_js_context_create_string(ctx, L"constructor"),
        ctx->std.async_function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, L"prototype"),
      neo_js_context_create_object(ctx, NULL, NULL), true, false, true);
  return result;
}

void neo_js_context_bind(neo_js_context_t ctx, neo_js_variable_t func,
                         neo_js_variable_t self) {
  neo_js_callable_t callable = neo_js_variable_to_callable(func);
  if (callable->bind) {
    neo_js_handle_t root = neo_js_scope_get_root_handle(ctx->scope);
    neo_js_handle_add_parent(callable->bind, root);
    callable->bind = NULL;
  }
  if (self) {
    neo_js_handle_t handle = neo_js_variable_get_handle(self);
    callable->bind = handle;
    neo_js_handle_add_parent(handle, neo_js_variable_get_handle(func));
  }
}

neo_js_variable_t neo_js_context_typeof(neo_js_context_t ctx,
                                        neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  const wchar_t *type = value->type->typeof_fn(ctx, variable);
  return neo_js_context_create_string(ctx, type);
}

neo_js_variable_t neo_js_context_to_string(neo_js_context_t ctx,
                                           neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_string_fn(ctx, self);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_boolean(neo_js_context_t ctx,
                                            neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_boolean_fn(ctx, self);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_number(neo_js_context_t ctx,
                                           neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_number_fn(ctx, self);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(allocator, current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_is_equal(neo_js_context_t ctx,
                                          neo_js_variable_t variable,
                                          neo_js_variable_t another) {
  neo_js_type_t lefttype = neo_js_variable_get_type(variable);
  neo_js_type_t righttype = neo_js_variable_get_type(another);
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  if (lefttype->kind != righttype->kind) {
    if (lefttype->kind == NEO_TYPE_NULL ||
        lefttype->kind == NEO_TYPE_UNDEFINED) {
      if (righttype->kind == NEO_TYPE_NULL ||
          righttype->kind == NEO_TYPE_UNDEFINED) {
        return neo_js_context_create_boolean(ctx, true);
      }
    }
    if (righttype->kind == NEO_TYPE_NULL ||
        righttype->kind == NEO_TYPE_UNDEFINED) {
      if (righttype->kind == NEO_TYPE_NULL ||
          righttype->kind == NEO_TYPE_UNDEFINED) {
        return neo_js_context_create_boolean(ctx, true);
      }
    }
    if (lefttype->kind >= NEO_TYPE_OBJECT &&
        righttype->kind < NEO_TYPE_OBJECT) {
      left = neo_js_context_to_primitive(ctx, left, L"default");
      if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
        return left;
      }
      lefttype = neo_js_variable_get_type(left);
    }
    if (righttype->kind >= NEO_TYPE_OBJECT &&
        lefttype->kind < NEO_TYPE_OBJECT) {
      right = neo_js_context_to_primitive(ctx, right, L"default");
      if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
        return right;
      }
      righttype = neo_js_variable_get_type(right);
    }
    if (lefttype->kind != righttype->kind) {
      if (lefttype->kind == NEO_TYPE_SYMBOL ||
          righttype->kind == NEO_TYPE_SYMBOL) {
        return neo_js_context_create_boolean(ctx, false);
      }
    }
    if (lefttype->kind == NEO_TYPE_BOOLEAN) {
      left = neo_js_context_to_number(ctx, left);
      return neo_js_context_is_equal(ctx, left, right);
    }
    if (righttype->kind == NEO_TYPE_BOOLEAN) {
      right = neo_js_context_to_number(ctx, right);
      return neo_js_context_is_equal(ctx, left, right);
    }
    if (lefttype->kind == NEO_TYPE_STRING &&
        righttype->kind == NEO_TYPE_NUMBER) {
      left = neo_js_context_to_number(ctx, left);
      lefttype = neo_js_variable_get_type(left);
    } else if (lefttype->kind == NEO_TYPE_NUMBER &&
               righttype->kind == NEO_TYPE_STRING) {
      right = neo_js_context_to_number(ctx, right);
      righttype = neo_js_variable_get_type(right);
    }
  }
  return neo_js_context_create_boolean(ctx,
                                       lefttype->is_equal_fn(ctx, left, right));
}

neo_js_variable_t neo_js_context_is_gt(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"number");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"number");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_TYPE_STRING && righttype->kind == NEO_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = wcscmp(lstring->string, rstring->string);
    return neo_js_context_create_boolean(ctx, res > 0);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_boolean(ctx, lnum->number > rnum->number);
}

neo_js_variable_t neo_js_context_is_lt(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"number");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"number");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_TYPE_STRING && righttype->kind == NEO_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = wcscmp(lstring->string, rstring->string);
    return neo_js_context_create_boolean(ctx, res < 0);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_boolean(ctx, lnum->number < rnum->number);
}

neo_js_variable_t neo_js_context_is_ge(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"number");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"number");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_TYPE_STRING && righttype->kind == NEO_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = wcscmp(lstring->string, rstring->string);
    return neo_js_context_create_boolean(ctx, res >= 0);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_boolean(ctx, lnum->number >= rnum->number);
}

neo_js_variable_t neo_js_context_is_le(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"number");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"number");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_TYPE_STRING && righttype->kind == NEO_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = wcscmp(lstring->string, rstring->string);
    return neo_js_context_create_boolean(ctx, res <= 0);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_boolean(ctx, lnum->number <= rnum->number);
}

neo_js_variable_t neo_js_context_add(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_TYPE_STRING || righttype->kind == NEO_TYPE_STRING) {
    left = neo_js_context_to_string(ctx, left);
    if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
      return left;
    }
    right = neo_js_context_to_string(ctx, right);
    if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
      return right;
    }
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    size_t len = wcslen(lstring->string);
    len += wcslen(rstring->string);
    len++;
    wchar_t *str = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
    swprintf(str, len, L"%ls%ls", lstring->string, rstring->string);
    neo_js_variable_t result = neo_js_context_create_string(ctx, str);
    neo_allocator_free(allocator, str);
    return result;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_number(ctx, lnum->number + rnum->number);
}

neo_js_variable_t neo_js_context_sub(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_number(ctx, lnum->number - rnum->number);
}

neo_js_variable_t neo_js_context_mul(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_number(ctx, lnum->number * rnum->number);
}

neo_js_variable_t neo_js_context_div(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_number(ctx, lnum->number / rnum->number);
}
neo_js_variable_t neo_js_context_mod(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  double value = lnum->number;
  while (value > rnum->number) {
    value -= rnum->number;
  }
  return neo_js_context_create_number(ctx, value);
}

neo_js_variable_t neo_js_context_pow(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_number(ctx, pow(lnum->number, rnum->number));
}

neo_js_variable_t neo_js_context_not(neo_js_context_t ctx,
                                     neo_js_variable_t variable) {
  neo_js_variable_t left = variable;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  int32_t i32 = lnum->number;
  return neo_js_context_create_number(ctx, ~i32);
}

neo_js_variable_t neo_js_context_instance_of(neo_js_context_t ctx,
                                             neo_js_variable_t variable,
                                             neo_js_variable_t constructor) {
  neo_js_type_t type = neo_js_variable_get_type(variable);
  if (type->kind != NEO_TYPE_OBJECT) {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_variable_t hasInstance = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"hasInstance"));
  if (hasInstance &&
      neo_js_variable_get_type(hasInstance)->kind == NEO_TYPE_CFUNCTION) {
    return neo_js_context_call(ctx, hasInstance, constructor, 1, &variable);
  }
  neo_js_object_t object = neo_js_variable_to_object(variable);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"prototype"));
  while (object) {
    if (object == neo_js_variable_to_object(prototype)) {
      return neo_js_context_create_boolean(ctx, true);
    }
    object = neo_js_value_to_object(neo_js_handle_get_value(object->prototype));
  }
  return neo_js_context_create_boolean(ctx, false);
}

neo_js_variable_t neo_js_context_and(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  int32_t ileft = lnum->number;
  int32_t iright = rnum->number;
  return neo_js_context_create_number(ctx, ileft & iright);
}

neo_js_variable_t neo_js_context_or(neo_js_context_t ctx,
                                    neo_js_variable_t variable,
                                    neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  int32_t ileft = lnum->number;
  int32_t iright = rnum->number;
  return neo_js_context_create_number(ctx, ileft | iright);
}

neo_js_variable_t neo_js_context_xor(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  int32_t ileft = lnum->number;
  int32_t iright = rnum->number;
  return neo_js_context_create_number(ctx, ileft ^ iright);
}

neo_js_variable_t neo_js_context_shr(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  int32_t ileft = lnum->number;
  int32_t iright = rnum->number;
  return neo_js_context_create_number(ctx, ileft >> iright);
}

neo_js_variable_t neo_js_context_shl(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  int32_t ileft = lnum->number;
  int32_t iright = rnum->number;
  return neo_js_context_create_number(ctx, ileft << iright);
}

neo_js_variable_t neo_js_context_ushr(neo_js_context_t ctx,
                                      neo_js_variable_t variable,
                                      neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  int32_t ileft = lnum->number;
  int32_t iright = rnum->number;
  uint32_t uleft = *(uint32_t *)&ileft;
  uleft >>= iright;
  return neo_js_context_create_number(ctx, uleft);
}

neo_js_variable_t neo_js_context_inc(neo_js_context_t ctx,
                                     neo_js_variable_t variable) {
  neo_js_variable_t vnum = neo_js_context_to_number(ctx, variable);
  if (neo_js_variable_get_type(vnum)->kind == NEO_TYPE_ERROR) {
    return vnum;
  }
  neo_js_number_t num = neo_js_variable_to_number(vnum);
  num->number += 1;
  return vnum;
}

neo_js_variable_t neo_js_context_dec(neo_js_context_t ctx,
                                     neo_js_variable_t variable) {
  neo_js_variable_t vnum = neo_js_context_to_number(ctx, variable);
  if (neo_js_variable_get_type(vnum)->kind == NEO_TYPE_ERROR) {
    return vnum;
  }
  neo_js_number_t num = neo_js_variable_to_number(vnum);
  num->number -= 1;
  return vnum;
}

neo_js_variable_t neo_js_context_logical_not(neo_js_context_t ctx,
                                             neo_js_variable_t variable) {
  neo_js_variable_t left = variable;
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  left = neo_js_context_to_boolean(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  neo_js_boolean_t boolean = neo_js_variable_to_boolean(left);
  return neo_js_context_create_boolean(ctx, !boolean->boolean);
}

neo_js_variable_t neo_js_context_concat(neo_js_context_t ctx,
                                        neo_js_variable_t variable,
                                        neo_js_variable_t another) {
  neo_js_variable_t left = neo_js_context_to_string(ctx, variable);
  if (neo_js_variable_get_type(left)->kind == NEO_TYPE_ERROR) {
    return left;
  }
  neo_js_variable_t right = neo_js_context_to_string(ctx, another);
  if (neo_js_variable_get_type(right)->kind == NEO_TYPE_ERROR) {
    return right;
  }
  neo_js_string_t lstring = neo_js_variable_to_string(left);
  neo_js_string_t rstring = neo_js_variable_to_string(right);
  size_t len = wcslen(lstring->string);
  len += wcslen(rstring->string);
  len += 1;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  wchar_t *str = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
  swprintf(str, len, L"%ls%ls", lstring->string, rstring->string);
  neo_js_variable_t result = neo_js_context_create_string(ctx, str);
  neo_allocator_free(allocator, str);
  return result;
}

neo_js_variable_t neo_js_context_eval(neo_js_context_t ctx, const char *file,
                                      const char *source) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  wchar_t *u16file = neo_string_to_wstring(allocator, file);
  neo_program_t program = neo_js_runtime_get_program(ctx->runtime, u16file);
  if (!program) {
    neo_ast_node_t root = TRY(neo_ast_parse_code(allocator, file, source)) {
      neo_allocator_free(allocator, u16file);
      neo_error_t err = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
      wchar_t *message =
          neo_string_to_wstring(allocator, neo_error_get_message(err));
      neo_js_variable_t error =
          neo_js_context_create_error(ctx, NEO_ERROR_SYNTAX, message);
      neo_allocator_free(allocator, message);
      neo_allocator_free(allocator, err);
      return error;
    };
    neo_variable_t json = neo_ast_node_serialize(allocator, root);
    char *json_str = neo_json_stringify(allocator, json);
    FILE *fp = fopen("../index.json", "w");
    fprintf(fp, "%s", json_str);
    fclose(fp);
    neo_allocator_free(allocator, json_str);
    neo_allocator_free(allocator, json);
    program = TRY(neo_ast_write_node(allocator, file, root)) {
      neo_allocator_free(allocator, root);
      neo_allocator_free(allocator, u16file);
      neo_error_t err = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
      wchar_t *message =
          neo_string_to_wstring(allocator, neo_error_get_message(err));
      neo_js_variable_t error =
          neo_js_context_create_error(ctx, NEO_ERROR_SYNTAX, message);
      neo_allocator_free(allocator, message);
      neo_allocator_free(allocator, err);
      return error;
    }
    neo_allocator_free(allocator, root);
    neo_js_runtime_set_program(ctx->runtime, u16file, program);
    fp = fopen("../index.asm", "w");
    TRY(neo_program_write(allocator, fp, program)) {
      fclose(fp);
      neo_allocator_free(allocator, u16file);
      neo_error_t err = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
      wchar_t *message =
          neo_string_to_wstring(allocator, neo_error_get_message(err));
      neo_js_variable_t error =
          neo_js_context_create_error(ctx, NEO_ERROR_SYNTAX, message);
      neo_allocator_free(allocator, message);
      neo_allocator_free(allocator, err);
      return error;
    }
    fclose(fp);
  }
  neo_allocator_free(allocator, u16file);
  neo_js_vm_t vm = neo_create_js_vm(ctx, NULL, 0);
  neo_js_variable_t result = neo_js_vm_eval(vm, program);
  neo_allocator_free(allocator, vm);
  return result;
}