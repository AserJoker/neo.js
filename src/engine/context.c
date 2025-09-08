#include "engine/context.h"
#include "compiler/ast/node.h"
#include "compiler/parser.h"
#include "compiler/program.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/bigint.h"
#include "core/buffer.h"
#include "core/clock.h"
#include "core/common.h"
#include "core/error.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "core/path.h"
#include "core/position.h"
#include "core/string.h"
#include "engine/basetype/array.h"
#include "engine/basetype/bigint.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/callable.h"
#include "engine/basetype/cfunction.h"
#include "engine/basetype/coroutine.h"
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
#include "engine/chunk.h"
#include "engine/lib/clear_interval.h"
#include "engine/lib/clear_timeout.h"
#include "engine/lib/decode_uri.h"
#include "engine/lib/decode_uri_component.h"
#include "engine/lib/encode_uri.h"
#include "engine/lib/encode_uri_component.h"
#include "engine/lib/eval.h"
#include "engine/lib/is_finite.h"
#include "engine/lib/is_nan.h"
#include "engine/lib/json.h"
#include "engine/lib/parse_float.h"
#include "engine/lib/parse_int.h"
#include "engine/lib/set_interval.h"
#include "engine/lib/set_timeout.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/stackframe.h"
#include "engine/std/aggregate_error.h"
#include "engine/std/array.h"
#include "engine/std/array_iterator.h"
#include "engine/std/async_function.h"
#include "engine/std/async_generator.h"
#include "engine/std/async_generator_function.h"
#include "engine/std/bigint.h"
#include "engine/std/boolean.h"
#include "engine/std/console.h"
#include "engine/std/date.h"
#include "engine/std/error.h"
#include "engine/std/function.h"
#include "engine/std/generator.h"
#include "engine/std/generator_function.h"
#include "engine/std/internal_error.h"
#include "engine/std/map.h"
#include "engine/std/math.h"
#include "engine/std/number.h"
#include "engine/std/object.h"
#include "engine/std/promise.h"
#include "engine/std/range_error.h"
#include "engine/std/reference_error.h"
#include "engine/std/reflect.h"
#include "engine/std/regexp.h"
#include "engine/std/set.h"
#include "engine/std/string.h"
#include "engine/std/symbol.h"
#include "engine/std/syntax_error.h"
#include "engine/std/type_error.h"
#include "engine/std/uri_error.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/vm.h"
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

typedef struct _neo_js_task_t *neo_js_task_t;

struct _neo_js_task_t {
  neo_js_chunk_t function;
  uint64_t start;
  uint64_t timeout;
  uint32_t id;
  uint32_t argc;
  neo_js_chunk_t *argv;
  neo_js_chunk_t self;
  bool keepalive;
};

typedef struct _neo_js_feature_t {
  neo_js_feature_fn_t enable_fn;
  neo_js_feature_fn_t disable_fn;
} *neo_js_feature_t;

struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t scope;
  neo_js_scope_t root;
  neo_js_scope_t task;
  neo_list_t stacktrace;
  neo_list_t micro_tasks;
  neo_list_t macro_tasks;
  neo_list_t coroutines;
  neo_hash_map_t modules;
  neo_hash_map_t features;
  neo_js_assert_fn_t assert_fn;
  neo_js_call_type_t call_type;
  neo_js_std_t std;
};

static void neo_js_context_init_lib(neo_js_context_t ctx) {
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "setTimeout"),
      neo_js_context_create_cfunction(ctx, "setTimeout", neo_js_set_timeout),
      true, true, true);
  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "clearTimeout"),
                           neo_js_context_create_cfunction(
                               ctx, "clearTimeout", neo_js_clear_timeout),
                           true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "setInterval"),
      neo_js_context_create_cfunction(ctx, "setInterval", neo_js_set_interval),
      true, true, true);
  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "clearInterval"),
                           neo_js_context_create_cfunction(
                               ctx, "clearInterval", neo_js_clear_interval),
                           true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "encodeURI"),
      neo_js_context_create_cfunction(ctx, "encodeURI", neo_js_encode_uri),
      true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global,
      neo_js_context_create_string(ctx, "encodeURIComponent"),
      neo_js_context_create_cfunction(ctx, "encodeURIComponent",
                                      neo_js_encode_uri_component),
      true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "decodeURI"),
      neo_js_context_create_cfunction(ctx, "decodeURI", neo_js_decode_uri),
      true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global,
      neo_js_context_create_string(ctx, "decodeURIComponent"),
      neo_js_context_create_cfunction(ctx, "decodeURIComponent",
                                      neo_js_decode_uri_component),
      true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "eval"),
      neo_js_context_create_cfunction(ctx, "eval", neo_js_eval), true, true,
      true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "isFinite"),
      neo_js_context_create_cfunction(ctx, "isFinite", neo_js_is_finite), true,
      true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "isNaN"),
      neo_js_context_create_cfunction(ctx, "isNaN", neo_js_is_nan), true, true,
      true);

  neo_js_variable_t parse_float =
      neo_js_context_create_cfunction(ctx, "parseFloat", neo_js_parse_float);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "parseFloat"),
                           parse_float, true, true, true);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, "parseFloat"),
                           parse_float, true, false, true);

  neo_js_variable_t parse_int =
      neo_js_context_create_cfunction(ctx, "parseInt", neo_js_parse_int);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "parseInt"),
                           parse_int, true, true, true);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, "parseInt"),
                           parse_int, true, false, true);

  neo_js_context_init_lib_json(ctx);
}

void neo_js_context_init_std(neo_js_context_t ctx) {
  ctx->std.object_constructor = neo_js_context_create_cfunction(
      ctx, "Object", &neo_js_object_constructor);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t prototype =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));

  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, "prototype"),
                           prototype, true, false, true);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, "constructor"),
                           ctx->std.object_constructor, true, false, true);

  neo_js_context_pop_scope(ctx);
  ctx->std.function_constructor = neo_js_context_create_cfunction(
      ctx, "Function", &neo_js_function_constructor);

  neo_js_context_push_scope(ctx);

  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, "constructor"),
                           ctx->std.function_constructor, true, false, true);

  neo_js_context_def_field(ctx, ctx->std.function_constructor,
                           neo_js_context_create_string(ctx, "prototype"),
                           neo_js_context_create_object(ctx, NULL), true, false,
                           true);

  neo_js_context_pop_scope(ctx);
  ctx->std.symbol_constructor =
      neo_js_context_create_cfunction(ctx, "Symbol", neo_js_symbol_constructor);

  neo_js_context_push_scope(ctx);

  neo_js_context_init_std_symbol(ctx);

  neo_js_context_init_std_object(ctx);

  neo_js_context_init_std_function(ctx);

  neo_js_context_pop_scope(ctx);

  ctx->std.error_constructor =
      neo_js_context_create_cfunction(ctx, "Error", neo_js_error_constructor);

  ctx->std.aggregate_error_constructor = neo_js_context_create_cfunction(
      ctx, "AggregateError", neo_js_aggregate_error_constructor);
  neo_js_context_extends(ctx, ctx->std.aggregate_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.range_error_constructor = neo_js_context_create_cfunction(
      ctx, "RangeError", neo_js_range_error_constructor);
  neo_js_context_extends(ctx, ctx->std.range_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.uri_error_constructor = neo_js_context_create_cfunction(
      ctx, "URIError", neo_js_uri_error_constructor);
  neo_js_context_extends(ctx, ctx->std.uri_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.type_error_constructor = neo_js_context_create_cfunction(
      ctx, "TypeError", neo_js_type_error_constructor);
  neo_js_context_extends(ctx, ctx->std.type_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.syntax_error_constructor = neo_js_context_create_cfunction(
      ctx, "SyntaxError", neo_js_syntax_error_constructor);
  neo_js_context_extends(ctx, ctx->std.syntax_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.reference_error_constructor = neo_js_context_create_cfunction(
      ctx, "ReferenceError", neo_js_reference_error_constructor);
  neo_js_context_extends(ctx, ctx->std.reference_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.internal_error_constructor = neo_js_context_create_cfunction(
      ctx, "InternalError", neo_js_internal_error_constructor);
  neo_js_context_extends(ctx, ctx->std.internal_error_constructor,
                         ctx->std.error_constructor);

  neo_js_variable_t array_prototype = neo_js_context_create_array(ctx);
  ctx->std.array_constructor =
      neo_js_context_create_cfunction(ctx, "Array", neo_js_array_constructor);

  neo_js_context_def_field(ctx, ctx->std.array_constructor,
                           neo_js_context_create_string(ctx, "prototype"),
                           array_prototype, true, false, true);

  neo_js_context_def_field(ctx, array_prototype,
                           neo_js_context_create_string(ctx, "constructor"),
                           ctx->std.array_constructor, true, false, true);

  ctx->std.array_iterator_constructor = neo_js_context_create_cfunction(
      ctx, "ArrayIterator", neo_js_array_iterator_constructor);

  ctx->std.generator_function_constructor = neo_js_context_create_cfunction(
      ctx, "GeneratorFunction", neo_js_generator_function_constructor);

  ctx->std.async_generator_function_constructor =
      neo_js_context_create_cfunction(
          ctx, "AsyncGeneratorFunction",
          neo_js_async_generator_function_constructor);

  ctx->std.async_function_constructor = neo_js_context_create_cfunction(
      ctx, "AsyncFunction", neo_js_async_function_constructor);

  ctx->std.async_generator_constructor = neo_js_context_create_cfunction(
      ctx, "AsyncGenerator", neo_js_async_generator_constructor);

  ctx->std.bigint_constructor =
      neo_js_context_create_cfunction(ctx, "BigInt", neo_js_bigint_constructor);

  ctx->std.boolean_constructor = neo_js_context_create_cfunction(
      ctx, "Boolean", neo_js_boolean_constructor);

  ctx->std.date_constructor =
      neo_js_context_create_cfunction(ctx, "Date", neo_js_date_constructor);

  ctx->std.generator_constructor = neo_js_context_create_cfunction(
      ctx, "Generator", neo_js_generator_constructor);

  ctx->std.map_constructor =
      neo_js_context_create_cfunction(ctx, "Map", neo_js_map_constructor);

  ctx->std.promise_constructor = neo_js_context_create_cfunction(
      ctx, "Promise", neo_js_promise_constructor);

  ctx->std.regexp_constructor =
      neo_js_context_create_cfunction(ctx, "RegExp", neo_js_regexp_constructor);

  ctx->std.number_constructor =
      neo_js_context_create_cfunction(ctx, "Number", neo_js_number_constructor);

  ctx->std.string_constructor = neo_js_context_create_cfunction(
      ctx, "String", &neo_js_string_constructor);

  ctx->std.set_constructor =
      neo_js_context_create_cfunction(ctx, "Set", &neo_js_set_constructor);

  ctx->std.global = neo_js_context_create_object(ctx, NULL);

  neo_js_context_push_scope(ctx);

  neo_js_context_init_std_array(ctx);

  neo_js_context_init_std_array_iterator(ctx);

  neo_js_context_init_std_async_generator_function(ctx);

  neo_js_context_init_std_async_function(ctx);

  neo_js_context_init_std_async_generator(ctx);

  neo_js_context_init_std_bigint(ctx);

  neo_js_context_init_std_boolean(ctx);

  neo_js_context_init_std_date(ctx);

  neo_js_context_init_std_aggregate_error(ctx);

  neo_js_context_init_std_error(ctx);

  neo_js_context_init_std_generator_function(ctx);

  neo_js_context_init_std_generator(ctx);

  neo_js_context_init_std_map(ctx);

  neo_js_context_init_std_math(ctx);

  neo_js_context_init_std_number(ctx);

  neo_js_context_init_std_promise(ctx);

  neo_js_context_init_std_reflect(ctx);

  neo_js_context_init_std_regexp(ctx);

  neo_js_context_init_std_set(ctx);

  neo_js_context_init_std_string(ctx);

  neo_js_context_extends(ctx, ctx->std.generator_function_constructor,
                         ctx->std.function_constructor);

  neo_js_context_extends(ctx, ctx->std.async_function_constructor,
                         ctx->std.function_constructor);

  neo_js_context_extends(ctx, ctx->std.async_generator_function_constructor,
                         ctx->std.function_constructor);

  neo_js_context_init_std_console(ctx);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "global"),
                           ctx->std.global, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Error"),
                           ctx->std.error_constructor, true, true, true);

  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "AggregateError"),
      ctx->std.aggregate_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "RangeError"),
                           ctx->std.range_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "URIError"),
                           ctx->std.uri_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "TypeError"),
                           ctx->std.type_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "SyntaxError"),
                           ctx->std.syntax_error_constructor, true, true, true);

  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, "ReferenceError"),
      ctx->std.reference_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Function"),
                           ctx->std.function_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Object"),
                           ctx->std.object_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Symbol"),
                           ctx->std.symbol_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Array"),
                           ctx->std.array_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Promise"),
                           ctx->std.promise_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "RegExp"),
                           ctx->std.regexp_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "BigInt"),
                           ctx->std.bigint_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Boolean"),
                           ctx->std.boolean_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Date"),
                           ctx->std.date_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Map"),
                           ctx->std.map_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, "Number"),
                           ctx->std.number_constructor, true, true, true);

  neo_js_context_pop_scope(ctx);
  neo_js_context_init_lib(ctx);
}

static neo_js_variable_t neo_js_context_default_assert(neo_js_context_t ctx,
                                                       const char *type,
                                                       const char *value,
                                                       const char *file) {
  return neo_js_context_create_undefined(ctx);
}

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t ctx) {
  neo_allocator_free(allocator, ctx->features);
  for (neo_list_node_t it = neo_list_get_first(ctx->coroutines);
       it != neo_list_get_tail(ctx->coroutines); it = neo_list_node_next(it)) {
    neo_js_co_context_t coroutine = neo_list_node_get(it);
    neo_allocator_free(allocator, coroutine->vm->root);
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
  neo_allocator_free(allocator, ctx->modules);
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
  ctx->assert_fn = neo_js_context_default_assert;
  memset(&ctx->std, 0, sizeof(ctx->std));
  neo_list_initialize_t initialize = {true};
  ctx->stacktrace = neo_create_list(allocator, &initialize);
  ctx->macro_tasks = neo_create_list(allocator, NULL);
  ctx->micro_tasks = neo_create_list(allocator, NULL);
  ctx->coroutines = neo_create_list(allocator, NULL);
  neo_hash_map_initialize_t module_initialize = {0};
  module_initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  module_initialize.compare = (neo_compare_fn_t)strcmp;
  module_initialize.auto_free_key = true;
  module_initialize.auto_free_value = false;
  ctx->modules = neo_create_hash_map(allocator, &module_initialize);
  neo_hash_map_initialize_t feature_initialize = {0};
  feature_initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  feature_initialize.compare = (neo_compare_fn_t)strcmp;
  feature_initialize.auto_free_key = true;
  feature_initialize.auto_free_value = true;
  ctx->features = neo_create_hash_map(allocator, &feature_initialize);
  neo_js_stackframe_t frame = neo_create_js_stackframe(allocator);
  frame->function = neo_create_string(allocator, "start");
  neo_list_push(ctx->stacktrace, frame);
  neo_js_context_push_stackframe(ctx, NULL, "_.eval", 0, 0);
  ctx->call_type = NEO_JS_FUNCTION_CALL;
  return ctx;
}

neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t ctx) {
  return ctx->runtime;
}

void neo_js_context_defer_free(neo_js_context_t ctx, void *data) {
  neo_js_scope_defer_free(ctx->scope, data);
}

void *neo_js_context_alloc_ex(neo_js_context_t ctx, size_t size,
                              neo_dispose_fn_t dispose, const char *filename,
                              size_t line) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  return neo_allocator_alloc_ex(allocator, size, dispose, filename, line);
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
    neo_js_chunk_remove_parent(task->function,
                               neo_js_scope_get_root_chunk(ctx->task));
  }
  if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
    error = neo_js_error_get_error(ctx, error);
    error = neo_js_context_to_string(ctx, error);
    neo_js_string_t serror = neo_js_variable_to_string(error);
    fprintf(stderr, "Uncaught %s\n", serror->string);
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
  if (neo_js_variable_get_type(callable)->kind < NEO_JS_TYPE_CALLABLE) {
    return 0;
  }
  static uint32_t id = 0;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_task_t task =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_task_t), NULL);
  neo_list_push(ctx->micro_tasks, task);
  id++;
  task->id = id;
  task->timeout = timeout;
  task->keepalive = keepalive;
  task->start = neo_clock_get_timestamp();
  task->function = neo_js_variable_get_chunk(callable);
  task->self = neo_js_variable_get_chunk(self);
  neo_js_chunk_add_parent(task->function,
                          neo_js_scope_get_root_chunk(ctx->task));
  neo_js_chunk_add_parent(task->self, task->function);
  task->argc = argc;
  if (argc) {
    task->argv =
        neo_allocator_alloc(allocator, sizeof(neo_js_chunk_t) * argc, NULL);
    for (uint32_t idx = 0; idx < argc; idx++) {
      task->argv[idx] = neo_js_variable_get_chunk(argv[idx]);
      neo_js_chunk_add_parent(task->argv[idx], task->function);
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
  if (neo_js_variable_get_type(callable)->kind < NEO_JS_TYPE_CALLABLE) {
    return 0;
  }
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
  task->function = neo_js_variable_get_chunk(callable);
  task->self = neo_js_variable_get_chunk(self);
  neo_js_chunk_add_parent(task->function,
                          neo_js_scope_get_root_chunk(ctx->task));
  neo_js_chunk_add_parent(task->self, task->function);
  task->argc = argc;
  if (argc) {
    task->argv =
        neo_allocator_alloc(allocator, sizeof(neo_js_chunk_t) * argc, NULL);
    for (uint32_t idx = 0; idx < argc; idx++) {
      task->argv[idx] = neo_js_variable_get_chunk(argv[idx]);
      neo_js_chunk_add_parent(task->argv[idx], task->function);
    }
  } else {
    task->argv = NULL;
  }
  return task->id;
}

void neo_js_context_kill_micro_task(neo_js_context_t ctx, uint32_t id) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_chunk_t hroot = neo_js_scope_get_root_chunk(ctx->task);
  for (neo_list_node_t it = neo_list_get_first(ctx->micro_tasks);
       it != neo_list_get_tail(ctx->micro_tasks); it = neo_list_node_next(it)) {
    neo_js_task_t task = neo_list_node_get(it);
    if (task->id == id) {
      neo_js_context_push_scope(ctx);
      neo_js_chunk_add_parent(task->function,
                              neo_js_scope_get_root_chunk(ctx->scope));
      neo_js_chunk_remove_parent(task->function, hroot);
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
  neo_js_chunk_t hroot = neo_js_scope_get_root_chunk(ctx->task);
  for (neo_list_node_t it = neo_list_get_first(ctx->macro_tasks);
       it != neo_list_get_tail(ctx->macro_tasks); it = neo_list_node_next(it)) {
    neo_js_task_t task = neo_list_node_get(it);
    if (task->id == id) {
      neo_js_context_push_scope(ctx);
      neo_js_chunk_add_parent(task->function,
                              neo_js_scope_get_root_chunk(ctx->scope));
      neo_js_chunk_remove_parent(task->function, hroot);
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

neo_list_t neo_js_context_clone_stacktrace(neo_js_context_t ctx) {
  neo_list_initialize_t initialize = {true};
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_list_t result = neo_create_list(allocator, &initialize);
  for (neo_list_node_t it = neo_list_get_first(ctx->stacktrace);
       it != neo_list_get_tail(ctx->stacktrace); it = neo_list_node_next(it)) {
    neo_js_stackframe_t frame = neo_list_node_get(it);
    neo_js_stackframe_t target = neo_create_js_stackframe(allocator);
    target->column = frame->column;
    target->line = frame->line;
    if (frame->filename) {
      target->filename = neo_create_string(allocator, frame->filename);
    }
    if (frame->function) {
      target->function = neo_create_string(allocator, frame->function);
    }
    neo_list_push(result, target);
  }
  return result;
}

neo_list_t neo_js_context_set_stacktrace(neo_js_context_t ctx,
                                         neo_list_t stacktrace) {
  neo_list_t current = ctx->stacktrace;
  ctx->stacktrace = stacktrace;
  return current;
}

void neo_js_context_push_stackframe(neo_js_context_t ctx, const char *filename,
                                    const char *function, uint32_t column,
                                    uint32_t line) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_list_node_t it = neo_list_get_last(ctx->stacktrace);
  neo_js_stackframe_t frame = neo_list_node_get(it);

  frame->column = column;
  frame->line = line;
  if (filename) {
    frame->filename = neo_create_string(allocator, filename);
  } else {
    frame->filename = NULL;
  }
  frame = neo_create_js_stackframe(allocator);
  if (function) {
    frame->function = neo_create_string(allocator, function);
  } else {
    frame->function = neo_create_string(allocator, "anonymous");
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

neo_js_std_t neo_js_context_get_std(neo_js_context_t ctx) { return ctx->std; }

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t ctx,
                                                 neo_js_chunk_t chunk,
                                                 const char *name) {
  return neo_js_scope_create_variable(ctx->scope, chunk, NULL);
}
neo_js_variable_t neo_js_context_create_ref_variable(neo_js_context_t ctx,
                                                     neo_js_handle_t handle,
                                                     const char *name) {
  return neo_js_scope_create_ref_variable(ctx->scope, handle, NULL);
}

neo_js_variable_t neo_js_context_def_variable(neo_js_context_t ctx,
                                              neo_js_variable_t variable,
                                              const char *name) {
  neo_js_variable_t current = neo_js_scope_get_variable(ctx->scope, name);
  if (current != NULL) {
    char msg[1024];
    snprintf(msg, 1024, "cannot redefine variable: '%s'", name);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX, 0, msg);
  }
  neo_js_chunk_t handle = neo_js_variable_get_chunk(variable);
  current = neo_js_scope_create_variable(ctx->scope, handle, name);
  neo_js_variable_set_const(current, neo_js_variable_is_const(variable));
  neo_js_variable_set_using(current, neo_js_variable_is_using(variable));
  neo_js_variable_set_await_using(current,
                                  neo_js_variable_is_await_using(variable));
  neo_js_variable_set_const(variable, false);
  neo_js_variable_set_using(variable, false);
  neo_js_variable_set_await_using(variable, false);
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_store_variable(neo_js_context_t ctx,
                                                neo_js_variable_t variable,
                                                const char *name) {
  neo_js_variable_t current = neo_js_context_load_variable(ctx, name);
  if (neo_js_variable_get_type(current)->kind == NEO_JS_TYPE_ERROR) {
    return current;
  }
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_ERROR) {
    return variable;
  }
  if (neo_js_variable_get_type(current)->kind != NEO_JS_TYPE_UNINITIALIZE) {
    if (neo_js_variable_is_const(current) ||
        neo_js_variable_is_using(current) ||
        neo_js_variable_is_await_using(current)) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0, "assignment to constant variable.");
    }
  }
  neo_js_type_t type = neo_js_variable_get_type(variable);
  neo_js_context_copy(ctx, variable, current);
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_load_variable(neo_js_context_t ctx,
                                               const char *name) {
  neo_js_scope_t scope = ctx->scope;
  while (scope) {
    neo_js_variable_t variable = neo_js_scope_get_variable(scope, name);
    if (variable) {
      return variable;
    }
    scope = neo_js_scope_get_parent(scope);
  }
  neo_js_variable_t field = neo_js_context_create_string(ctx, name);
  if (neo_js_context_has_field(ctx, ctx->std.global, field)) {
    return neo_js_context_get_field(ctx, ctx->std.global, field, NULL);
  }
  char msg[1024];
  snprintf(msg, 1024, "variable '%s' is not defined", name);
  return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_REFERENCE, 0,
                                            msg);
}

neo_js_variable_t neo_js_context_extends(neo_js_context_t ctx,
                                         neo_js_variable_t variable,
                                         neo_js_variable_t parent) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, variable, neo_js_context_create_string(ctx, "prototype"), NULL);

  neo_js_variable_t parent_prototype = neo_js_context_get_field(
      ctx, parent, neo_js_context_create_string(ctx, "prototype"), NULL);

  return neo_js_object_set_prototype(ctx, prototype, parent_prototype);
}

neo_js_variable_t neo_js_context_to_primitive(neo_js_context_t ctx,
                                              neo_js_variable_t variable,
                                              const char *hint) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_value_t value = neo_js_variable_get_value(variable);
  neo_js_variable_t result = value->type->to_primitive_fn(ctx, variable, hint);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_object(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_value_t value = neo_js_variable_get_value(variable);
  neo_js_variable_t result = value->type->to_object_fn(ctx, variable);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field,
                                           neo_js_variable_t receiver) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot read properties of null (reading '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot read properties of undefined (reading '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->get_field_fn(ctx, object, field, NULL);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  result = neo_js_context_clone(ctx, result);
  return result;
}

neo_js_variable_t neo_js_context_set_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field,
                                           neo_js_variable_t value,
                                           neo_js_variable_t receiver) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set properties of null (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set properties of undefined (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  value = neo_js_context_clone(ctx, value);
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result =
      type->set_field_fn(ctx, object, field, value, NULL);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_private(neo_js_context_t ctx,
                                             neo_js_variable_t object,
                                             neo_js_variable_t field,
                                             neo_js_variable_t receiver) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot read privates of null (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot read privates of undefined (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_string_t key = neo_js_variable_to_string(field);
  if (!obj->privates) {
    size_t len = strlen(key->string) + 64;
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Private field '%s' must be declared in an enclosing class.",
        key->string);
  }
  neo_js_object_private_t prop =
      neo_hash_map_get(obj->privates, key->string, NULL, NULL);
  if (prop) {
    if (prop->value) {
      return neo_js_context_create_variable(ctx, prop->value, NULL);
    } else {
      if (prop->get) {
        neo_js_variable_t getter =
            neo_js_context_create_variable(ctx, prop->get, NULL);
        return neo_js_context_call(ctx, getter, receiver ? receiver : object, 0,
                                   NULL);
      } else {
        size_t len = strlen(key->string) + 64;
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, len, "Private field '%s' hasn't getter.",
            key->string);
      }
    }
  } else {
    size_t len = strlen(key->string) + 64;
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Private field '%s' must be declared in an enclosing class.",
        key->string);
  }
}

neo_js_variable_t neo_js_context_set_private(neo_js_context_t ctx,
                                             neo_js_variable_t object,
                                             neo_js_variable_t field,
                                             neo_js_variable_t value,
                                             neo_js_variable_t receiver) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set privates of null (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set privates of undefined (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_string_t key = neo_js_variable_to_string(field);
  if (!obj->privates) {
    size_t len = strlen(key->string) + 64;
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Private field '%s' must be declared in an enclosing class.",
        key->string);
  }
  neo_js_object_private_t prop =
      neo_hash_map_get(obj->privates, key->string, NULL, NULL);
  if (!prop) {
    size_t len = strlen(key->string) + 64;
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, len,
        "Private field '%s' must be declared in an enclosing class.",
        key->string);
  } else {
    if (prop->value) {
      neo_js_chunk_t root = neo_js_scope_get_root_chunk(ctx->scope);
      neo_js_chunk_add_parent(prop->value, root);
      prop->value = neo_js_variable_get_chunk(value);
      neo_js_chunk_add_parent(prop->value, neo_js_variable_get_chunk(object));
    } else if (prop->set) {
      neo_js_variable_t setter =
          neo_js_context_create_variable(ctx, prop->set, NULL);
      return neo_js_context_call(ctx, setter, receiver ? receiver : object, 1,
                                 &value);
    } else {
      size_t len = strlen(key->string) + 64;
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, len, "Private field '%s' is readonly.",
          key->string);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_def_private(neo_js_context_t ctx,
                                             neo_js_variable_t object,
                                             neo_js_variable_t field,
                                             neo_js_variable_t value) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set privates of null (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set privates of undefined (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  neo_js_string_t key = neo_js_variable_to_string(field);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  if (!obj->privates) {
    neo_hash_map_initialize_t initialize = {0};
    initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
    initialize.compare = (neo_compare_fn_t)strcmp;
    initialize.auto_free_key = true;
    initialize.auto_free_value = true;
    obj->privates = neo_create_hash_map(allocator, &initialize);
  }
  if (neo_hash_map_has(obj->privates, key->string, NULL, NULL)) {
    neo_js_string_t str = neo_js_variable_to_string(field);
    size_t len = strlen(str->string) + 64;
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_SYNTAX, len,
        "Identifier '%s' has already been declared", str->string);
  }
  neo_js_object_private_t prop = neo_create_js_object_private(allocator);
  prop->value = neo_js_variable_get_chunk(value);
  neo_hash_map_set(obj->privates, neo_create_string(allocator, key->string),
                   prop, NULL, NULL);
  neo_js_chunk_add_parent(prop->value, neo_js_variable_get_chunk(object));
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_def_private_accessor(
    neo_js_context_t ctx, neo_js_variable_t object, neo_js_variable_t field,
    neo_js_variable_t getter, neo_js_variable_t setter) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set privates of null (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set privates of undefined (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  neo_js_string_t key = neo_js_variable_to_string(field);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  if (!obj->privates) {
    neo_hash_map_initialize_t initialize = {0};
    initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
    initialize.compare = (neo_compare_fn_t)strcmp;
    initialize.auto_free_key = true;
    initialize.auto_free_value = true;
    obj->privates = neo_create_hash_map(allocator, &initialize);
  }
  neo_js_object_private_t prop =
      neo_hash_map_get(obj->privates, key->string, NULL, NULL);
  if (!prop) {
    prop = neo_create_js_object_private(allocator);
    if (getter) {
      prop->get = neo_js_variable_get_chunk(getter);
      neo_js_chunk_add_parent(prop->get, neo_js_variable_get_chunk(object));
    }
    if (setter) {
      prop->set = neo_js_variable_get_chunk(setter);
      neo_js_chunk_add_parent(prop->set, neo_js_variable_get_chunk(object));
    }
    neo_hash_map_set(obj->privates, neo_create_string(allocator, key->string),
                     prop, NULL, NULL);
  } else {
    if (prop->value || (prop->get && getter) || (prop->set && setter)) {
      neo_js_string_t str = neo_js_variable_to_string(field);
      size_t len = strlen(str->string) + 64;
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_SYNTAX, len,
          "Identifier '%s' has already been declared", str->string);
    }
    if (getter) {
      prop->get = neo_js_variable_get_chunk(getter);
      neo_js_chunk_add_parent(prop->get, neo_js_variable_get_chunk(object));
    }
    if (setter) {
      prop->set = neo_js_variable_get_chunk(setter);
      neo_js_chunk_add_parent(prop->set, neo_js_variable_get_chunk(object));
    }
  }
  return neo_js_context_create_undefined(ctx);
}

bool neo_js_context_has_field(neo_js_context_t ctx, neo_js_variable_t object,
                              neo_js_variable_t field) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot read properties of null (reading '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot read properties of undefined (reading '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  return neo_js_object_get_property(ctx, object, field) != NULL;
}

neo_js_variable_t
neo_js_context_def_field(neo_js_context_t ctx, neo_js_variable_t object,
                         neo_js_variable_t field, neo_js_variable_t value,
                         bool configurable, bool enumable, bool writable) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set properties of null (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set properties of undefined (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Object.defineProperty called on non-object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  field = neo_js_context_to_primitive(ctx, field, "default");
  if (neo_js_variable_get_type(field)->kind != NEO_JS_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  value = neo_js_context_clone(ctx, value);
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(ctx, object, field);
  neo_js_chunk_t hvalue = neo_js_variable_get_chunk(value);
  neo_js_chunk_t hobject = neo_js_variable_get_chunk(object);
  if (!prop) {
    if (obj->sealed || obj->frozen || !obj->extensible) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                                "Object is not extensible");
    }
    prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumerable = enumable;
    prop->writable = writable;
    prop->value = hvalue;
    prop->get = NULL;
    prop->set = NULL;
    neo_js_chunk_t hfield = neo_js_variable_get_chunk(field);
    neo_hash_map_set(obj->properties, hfield, prop, ctx, ctx);
    neo_js_chunk_add_parent(hvalue, hobject);
    neo_js_chunk_add_parent(hfield, hobject);
    if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_STRING &&
        prop->enumerable) {
      neo_list_push(obj->keys, hfield);
    }
    if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
      neo_list_push(obj->symbol_keys, hfield);
    }
  } else {
    if (obj->frozen) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                                "Object is not extensible");
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
        const char *field_name = neo_js_context_to_error_name(ctx, field);
        size_t len = strlen(field_name) + 64;
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, len,
            "Cannot assign to read only property '%s' of object "
            "'#<Object>'",
            field_name);
      }
      neo_js_chunk_remove_parent(prop->value, hobject);
      neo_js_chunk_add_parent(prop->value,
                              neo_js_scope_get_root_chunk(ctx->scope));
    } else {
      if (!prop->configurable) {
        const char *field_name = neo_js_context_to_error_name(ctx, field);
        size_t len = strlen(field_name) + 64;
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, len, "Cannot redefine property: '%s'",
            field_name);
      }
      if (prop->get) {
        neo_js_chunk_add_parent(prop->get,
                                neo_js_scope_get_root_chunk(ctx->scope));
        neo_js_chunk_remove_parent(prop->get, hobject);
        prop->get = NULL;
      }
      if (prop->set) {
        neo_js_chunk_add_parent(prop->set,
                                neo_js_scope_get_root_chunk(ctx->scope));
        neo_js_chunk_remove_parent(prop->set, hobject);
        prop->set = NULL;
      }
    }
    neo_js_chunk_add_parent(hvalue, hobject);
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
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set properties of null (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    const char *field_name = neo_js_context_to_error_name(ctx, field);
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, strlen(field_name) + 64,
        "Cannot set properties of undefined (setting '%s')", field_name);
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Object.defineProperty called on non-object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  field = neo_js_context_to_primitive(ctx, field, "default");
  if (neo_js_variable_get_type(field)->kind != NEO_JS_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(ctx, object, field);
  neo_js_chunk_t hobject = neo_js_variable_get_chunk(object);
  neo_js_chunk_t hgetter = NULL;
  neo_js_chunk_t hsetter = NULL;
  if (getter) {
    hgetter = neo_js_variable_get_chunk(getter);
  }
  if (setter) {
    hsetter = neo_js_variable_get_chunk(setter);
  }
  neo_js_chunk_t hfield = neo_js_variable_get_chunk(field);
  if (!prop) {
    if (obj->sealed || obj->frozen || !obj->extensible) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                                "Object is not extensible");
    }
    prop = neo_create_js_object_property(allocator);
    prop->configurable = configurable;
    prop->enumerable = enumable;
    prop->get = hgetter;
    prop->set = hsetter;
    if (hgetter) {
      neo_js_chunk_add_parent(hgetter, hobject);
    }
    if (hsetter) {
      neo_js_chunk_add_parent(hsetter, hobject);
    }
    neo_hash_map_set(obj->properties, hfield, prop, ctx, ctx);
    neo_js_chunk_add_parent(hfield, hobject);
  } else {
    if (prop->value != NULL) {
      if (!prop->configurable) {
        const char *field_name = neo_js_context_to_error_name(ctx, field);
        size_t len = strlen(field_name) + 64;
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, len,
            "Cannot assign to read only property '%s' of object "
            "'#<Object>'",
            field_name);
      }
      neo_js_chunk_add_parent(prop->value,
                              neo_js_scope_get_root_chunk(ctx->scope));
      neo_js_chunk_remove_parent(prop->value, hobject);
      prop->value = NULL;
      prop->writable = false;
    } else {
      if (!prop->configurable) {
        const char *field_name = neo_js_context_to_error_name(ctx, field);
        size_t len = strlen(field_name) + 64;
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, len, "Cannot redefine property: '%s'",
            field_name);
      }
    }
    if (prop->get) {
      neo_js_chunk_add_parent(prop->get,
                              neo_js_scope_get_root_chunk(ctx->scope));
      neo_js_chunk_remove_parent(prop->get, hobject);
      prop->get = NULL;
    }
    if (prop->set) {
      neo_js_chunk_add_parent(prop->set,
                              neo_js_scope_get_root_chunk(ctx->scope));
      neo_js_chunk_remove_parent(prop->set, hobject);
      prop->set = NULL;
    }
    if (hgetter) {
      neo_js_chunk_add_parent(hgetter, hobject);
      prop->get = hgetter;
    }
    if (hsetter) {
      neo_js_chunk_add_parent(hsetter, hobject);
      prop->set = hsetter;
    }
  }
  return object;
}

neo_js_variable_t neo_js_context_get_internal(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              const char *field) {
  neo_js_object_t object = neo_js_variable_to_object(self);
  if (object) {
    neo_js_chunk_t internal =
        neo_hash_map_get(object->internal, field, NULL, NULL);
    if (!internal) {
      return neo_js_context_create_undefined(ctx);
    }
    return neo_js_context_create_variable(ctx, internal, NULL);
  }
  return neo_js_context_create_undefined(ctx);
}

bool neo_js_context_has_internal(neo_js_context_t ctx, neo_js_variable_t self,
                                 const char *field) {
  neo_js_object_t object = neo_js_variable_to_object(self);
  if (object) {
    neo_js_chunk_t internal =
        neo_hash_map_get(object->internal, field, NULL, NULL);
    if (!internal) {
      return false;
    }
    return true;
  }
  return false;
}

void neo_js_context_set_internal(neo_js_context_t ctx, neo_js_variable_t self,
                                 const char *field, neo_js_variable_t value) {
  neo_js_object_t object = neo_js_variable_to_object(self);
  if (object) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_js_chunk_t current =
        neo_hash_map_get(object->internal, field, NULL, NULL);
    if (current) {
      neo_js_chunk_add_parent(current, neo_js_scope_get_root_chunk(ctx->scope));
    }
    neo_js_chunk_t hvalue = neo_js_variable_get_chunk(value);
    neo_hash_map_set(object->internal, neo_create_string(allocator, field),
                     hvalue, NULL, NULL);
    neo_js_chunk_add_parent(hvalue, neo_js_variable_get_chunk(self));
  }
}

void *neo_js_context_get_opaque(neo_js_context_t ctx,
                                neo_js_variable_t variable, const char *key) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return neo_hash_map_get(value->opaque, key, NULL, NULL);
}

void neo_js_context_set_opaque(neo_js_context_t ctx, neo_js_variable_t variable,
                               const char *field, void *value) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  size_t len = strlen(field);
  char *key = neo_allocator_alloc(allocator, sizeof(char) * (len + 1), NULL);
  strcpy(key, field);
  neo_js_value_t val = neo_js_variable_get_value(variable);
  neo_hash_map_set(val->opaque, key, value, NULL, NULL);
}

neo_js_variable_t neo_js_context_del_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field) {
  if (neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(object)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Cannot convert undefined or null to object");
  }
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    object = neo_js_context_to_object(ctx, object);
    NEO_JS_TRY_AND_THROW(object);
  }
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->del_field_fn(ctx, object, field);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_keys(neo_js_context_t ctx,
                                          neo_js_variable_t variable) {
  if (neo_js_variable_get_type(variable)->kind < NEO_JS_TYPE_OBJECT) {
    variable = neo_js_context_to_object(ctx, variable);
    NEO_JS_TRY_AND_THROW(variable);
  }
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_list_t keys = neo_js_object_get_keys(ctx, variable);
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(keys);
       it != neo_list_get_tail(keys); it = neo_list_node_next(it)) {
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_number(ctx, idx),
                             neo_list_node_get(it), NULL);
    idx++;
  }
  neo_allocator_free(allocator, keys);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_clone(neo_js_context_t ctx,
                                       neo_js_variable_t self) {
  neo_js_type_t type = neo_js_variable_get_type(self);
  if (type->kind < NEO_JS_TYPE_OBJECT && type->kind != NEO_JS_TYPE_SYMBOL) {
    neo_js_variable_t variable = neo_js_context_create_undefined(ctx);
    neo_js_variable_t err = neo_js_context_copy(ctx, self, variable);
    NEO_JS_TRY_AND_THROW(err);
    return variable;
  } else {
    return neo_js_context_create_variable(ctx, neo_js_variable_get_chunk(self),
                                          NULL);
  }
}

neo_js_variable_t neo_js_context_copy(neo_js_context_t ctx,
                                      neo_js_variable_t self,
                                      neo_js_variable_t target) {
  neo_js_type_t type = neo_js_variable_get_type(self);
  return type->copy_fn(ctx, self, target);
}

static neo_js_variable_t
neo_js_context_call_cfunction(neo_js_context_t ctx, neo_js_variable_t callee,
                              neo_js_variable_t self, uint32_t argc,
                              neo_js_variable_t *argv) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
  neo_js_scope_t current = neo_js_context_set_scope(ctx, scope);
  neo_js_variable_t bind = neo_js_callable_get_bind(ctx, callee);
  if (bind) {
    self = bind;
  } else {
    self = neo_js_scope_create_variable(scope, neo_js_variable_get_chunk(self),
                                        NULL);
  }
  neo_js_variable_t result = neo_js_context_create_undefined(ctx);
  neo_js_cfunction_t cfunction = neo_js_variable_to_cfunction(callee);
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t arg = argv[idx];
    neo_js_chunk_t harg = neo_js_variable_get_chunk(arg);
    neo_js_chunk_add_parent(harg, neo_js_scope_get_root_chunk(scope));
  }
  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(cfunction->callable.closure);
       it != neo_hash_map_get_tail(cfunction->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const char *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_ref_variable(scope, hvalue, name);
  }
  result = cfunction->callee(ctx, self, argc, argv);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_set_scope(ctx, current);
  neo_allocator_free(allocator, scope);
  return result;
}

static neo_js_variable_t neo_js_context_call_generator_function(
    neo_js_context_t ctx, neo_js_variable_t callee, neo_js_variable_t self,
    uint32_t argc, neo_js_variable_t *argv) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
  neo_js_function_t function = neo_js_variable_to_function(callee);
  neo_js_variable_t result =
      neo_js_context_construct(ctx, ctx->std.generator_constructor, 0, NULL);
  neo_js_scope_t current = neo_js_context_set_scope(ctx, scope);
  neo_js_variable_t bind = neo_js_callable_get_bind(ctx, callee);
  if (bind) {
    self = bind;
  } else {
    self = neo_js_context_create_variable(ctx, neo_js_variable_get_chunk(self),
                                          NULL);
  }
  neo_js_variable_t clazz = neo_js_callable_get_class(ctx, callee);
  neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL);
  neo_js_scope_set_variable(scope, arguments, "arguments");
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_context_set_field(ctx, arguments,
                             neo_js_context_create_number(ctx, idx), argv[idx],
                             NULL);
  }
  neo_js_context_set_field(ctx, arguments,
                           neo_js_context_create_string(ctx, "length"),
                           neo_js_context_create_number(ctx, argc), NULL);

  neo_js_context_set_field(
      ctx, arguments,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, "iterator"),
                               NULL),
      neo_js_context_create_cfunction(ctx, "values", neo_js_array_values),
      NULL);

  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const char *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_ref_variable(scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  neo_js_vm_t vm = neo_create_js_vm(ctx, self, clazz, function->address, scope);
  neo_js_variable_t coroutine =
      neo_js_context_create_coroutine(ctx, vm, function->program);
  neo_js_context_set_internal(ctx, result, "[[coroutine]]", coroutine);
  return result;
}

bool neo_js_context_is_thenable(neo_js_context_t ctx,
                                neo_js_variable_t variable) {
  if (neo_js_variable_get_type(variable)->kind < NEO_JS_TYPE_OBJECT) {
    return false;
  }
  neo_js_variable_t then = neo_js_context_get_field(
      ctx, variable, neo_js_context_create_string(ctx, "then"), NULL);
  if (neo_js_variable_get_type(then)->kind < NEO_JS_TYPE_CALLABLE) {
    return false;
  }
  return true;
}

neo_js_variable_t neo_js_context_create_coroutine(neo_js_context_t ctx,
                                                  neo_js_vm_t vm,
                                                  neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_co_context_t co_ctx = neo_create_js_co_context(
      allocator, vm, program, neo_js_context_clone_stacktrace(ctx));
  neo_list_push(ctx->coroutines, co_ctx);
  neo_js_coroutine_t coroutine = neo_create_js_coroutine(allocator, co_ctx);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &coroutine->value), NULL);
}

neo_js_variable_t neo_js_context_create_native_coroutine(
    neo_js_context_t ctx, neo_js_async_cfunction_fn_t callee,
    neo_js_variable_t self, uint32_t argc, neo_js_variable_t *argv,
    neo_js_scope_t scope) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_co_context_t co_ctx = neo_create_js_native_co_context(
      allocator, callee, self, argc, argv, scope,
      neo_js_context_clone_stacktrace(ctx));
  neo_list_push(ctx->coroutines, co_ctx);
  neo_js_coroutine_t coroutine = neo_create_js_coroutine(allocator, co_ctx);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &coroutine->value), NULL);
}

void neo_js_context_recycle_coroutine(neo_js_context_t ctx,
                                      neo_js_variable_t coroutine) {
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (co_ctx->vm) {
    neo_allocator_free(allocator, co_ctx->vm->root);
    co_ctx->vm->root = NULL;
    co_ctx->vm->scope = NULL;
  } else {
    neo_allocator_free(allocator, co_ctx->argv);
    neo_allocator_free(allocator, co_ctx->scope);
  }
  neo_list_delete(ctx->coroutines, co_ctx);
}

void neo_js_context_recycle(neo_js_context_t ctx, neo_js_chunk_t chunk) {
  neo_js_chunk_t current = neo_js_scope_get_root_chunk(ctx->scope);
  neo_js_chunk_add_parent(chunk, current);
}

static neo_js_variable_t neo_js_awaiter_on_fulfilled(neo_js_context_t ctx,
                                                     neo_js_variable_t self,
                                                     uint32_t argc,
                                                     neo_js_variable_t *argv) {
  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, "#task");
  neo_js_variable_t value = NULL;
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_js_scope_t current = NULL;
  if (co_ctx->vm) {
    current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
  } else {
    current = neo_js_context_set_scope(ctx, co_ctx->scope);
  }
  if (argc > 0) {
    value = neo_js_context_create_variable(
        ctx, neo_js_variable_get_chunk(argv[0]), NULL);
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_context_set_scope(ctx, current);
  if (co_ctx->callee) {
    co_ctx->result = neo_js_variable_get_chunk(value);
  } else {
    neo_list_t stack = co_ctx->vm->stack;
    neo_list_push(stack, value);
  }
  return neo_js_context_call(ctx, task, neo_js_context_create_undefined(ctx), 0,
                             NULL);
}

static neo_js_variable_t neo_js_awaiter_on_rejected(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    uint32_t argc,
                                                    neo_js_variable_t *argv) {
  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, "#task");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_js_scope_t current = NULL;
  if (co_ctx->vm) {
    current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
  } else {
    current = neo_js_context_set_scope(ctx, co_ctx->scope);
  }
  neo_js_variable_t value = neo_js_context_create_error(ctx, argv[0]);
  neo_js_context_set_scope(ctx, current);
  if (co_ctx->callee) {
    co_ctx->result = neo_js_variable_get_chunk(value);
  } else {
    neo_list_t stack = co_ctx->vm->stack;
    neo_list_push(stack, value);
    co_ctx->vm->offset = neo_buffer_get_size(co_ctx->program->codes);
  }
  return neo_js_context_call(ctx, task, neo_js_context_create_undefined(ctx), 0,
                             NULL);
}

static neo_js_variable_t neo_js_awaiter_task(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {

  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, "#resolve");
  neo_js_variable_t reject = neo_js_context_load_variable(ctx, "#reject");
  neo_js_variable_t on_fulfilled =
      neo_js_context_load_variable(ctx, "#onFulfilled");
  neo_js_variable_t on_rejected =
      neo_js_context_load_variable(ctx, "#onRejected");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, "#task");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_list_t stacktrace =
      neo_js_context_set_stacktrace(ctx, co_ctx->stacktrace);
  neo_js_context_push_stackframe(ctx, NULL, "_.awaiter", 0, 0);
  neo_js_variable_t value = NULL;
  if (co_ctx->callee) {
    neo_js_variable_t last = NULL;
    if (co_ctx->result) {
      last = neo_js_context_create_variable(ctx, co_ctx->result, NULL);
    } else {
      last = neo_js_context_create_undefined(ctx);
    }
    neo_js_scope_t current = neo_js_context_set_scope(ctx, co_ctx->scope);
    value = co_ctx->callee(ctx, co_ctx->self, co_ctx->argc, co_ctx->argv, last,
                           co_ctx->stage);
    neo_js_context_set_scope(ctx, current);
  } else if (co_ctx->vm) {
    value = neo_js_vm_exec(co_ctx->vm, co_ctx->program);
  } else {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_INTERNAL, 0,
                                              "Broken coroutine context");
  }
  neo_js_context_pop_stackframe(ctx);
  neo_js_context_set_stacktrace(ctx, stacktrace);
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_INTERRUPT) {
    neo_js_interrupt_t interrupt = neo_js_variable_to_interrupt(value);
    value = neo_js_context_create_variable(ctx, interrupt->result, NULL);
    if (co_ctx->callee) {
      co_ctx->stage = interrupt->offset;
    } else if (co_ctx->vm) {
      co_ctx->vm->offset = interrupt->offset;
    }
    if (neo_js_context_is_thenable(ctx, value)) {
      neo_js_variable_t then = neo_js_context_get_field(
          ctx, value, neo_js_context_create_string(ctx, "then"), NULL);
      if (interrupt->type == NEO_JS_INTERRUPT_BACKEND_AWAIT) {
        neo_js_variable_t args[] = {task, on_rejected};
        neo_js_context_call(ctx, then, value, 2, args);
      } else {
        neo_js_variable_t args[] = {on_fulfilled, on_rejected};
        neo_js_context_call(ctx, then, value, 2, args);
      }
    } else {
      neo_js_scope_t current = NULL;
      if (co_ctx->vm) {
        current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
      } else {
        current = neo_js_context_set_scope(ctx, co_ctx->scope);
      }
      neo_js_variable_t val =
          neo_js_context_create_variable(ctx, interrupt->result, NULL);
      if (co_ctx->callee) {
        co_ctx->result = neo_js_variable_get_chunk(val);
      } else {
        neo_list_t stack = co_ctx->vm->stack;
        neo_list_push(stack, val);
      }
      neo_js_context_set_scope(ctx, current);
      neo_js_context_create_micro_task(
          ctx, task, neo_js_context_create_undefined(ctx), 0, NULL, 0, false);
    }
  } else {
    if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
      value = neo_js_error_get_error(ctx, value);
      neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                          &value);
    } else {
      neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                          &value);
    }
    neo_js_context_recycle_coroutine(ctx, coroutine);
  }
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t neo_js_awaiter_resolver(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 uint32_t argc,
                                                 neo_js_variable_t *argv) {
  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t task =
      neo_js_context_create_cfunction(ctx, NULL, &neo_js_awaiter_task);
  neo_js_variable_t on_fulfilled =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_on_fulfilled);
  neo_js_variable_t on_rejected =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_on_rejected);

  neo_js_callable_set_closure(ctx, task, "#coroutine", coroutine);
  neo_js_callable_set_closure(ctx, task, "#resolve", resolve);
  neo_js_callable_set_closure(ctx, task, "#reject", reject);
  neo_js_callable_set_closure(ctx, task, "#onFulfilled", on_fulfilled);
  neo_js_callable_set_closure(ctx, task, "#onRejected", on_rejected);
  neo_js_callable_set_closure(ctx, task, "#task", task);

  neo_js_callable_set_closure(ctx, on_fulfilled, "#task", task);
  neo_js_callable_set_closure(ctx, on_fulfilled, "#coroutine", coroutine);

  neo_js_callable_set_closure(ctx, on_rejected, "#task", task);
  neo_js_callable_set_closure(ctx, on_rejected, "#coroutine", coroutine);

  neo_js_context_create_micro_task(
      ctx, task, neo_js_context_create_undefined(ctx), 0, NULL, 0, false);
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t neo_js_context_call_async_function(
    neo_js_context_t ctx, neo_js_variable_t callee, neo_js_variable_t self,
    uint32_t argc, neo_js_variable_t *argv) {
  neo_js_function_t function = neo_js_variable_to_function(callee);
  neo_js_variable_t resolver =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_resolver);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
  neo_js_scope_t current = neo_js_context_set_scope(ctx, scope);
  neo_js_variable_t bind = neo_js_callable_get_bind(ctx, callee);
  if (bind) {
    self = bind;
  } else {
    self = neo_js_context_create_variable(ctx, neo_js_variable_get_chunk(self),
                                          NULL);
  }
  neo_js_variable_t clazz = neo_js_callable_get_class(ctx, callee);
  neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL);
  neo_js_scope_set_variable(scope, arguments, "arguments");
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_context_set_field(ctx, arguments,
                             neo_js_context_create_number(ctx, idx), argv[idx],
                             NULL);
  }
  neo_js_context_set_field(ctx, arguments,
                           neo_js_context_create_string(ctx, "length"),
                           neo_js_context_create_number(ctx, argc), NULL);

  neo_js_context_set_field(
      ctx, arguments,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, "iterator"),
                               NULL),
      neo_js_context_create_cfunction(ctx, "values", neo_js_array_values),
      NULL);

  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const char *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_ref_variable(scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  neo_js_vm_t vm = neo_create_js_vm(ctx, self, clazz, function->address, scope);
  neo_js_variable_t coroutine =
      neo_js_context_create_coroutine(ctx, vm, function->program);
  neo_js_callable_set_closure(ctx, resolver, "#coroutine", coroutine);
  return neo_js_context_construct(ctx, ctx->std.promise_constructor, 1,
                                  &resolver);
}

static neo_js_variable_t neo_js_context_call_async_generator_function(
    neo_js_context_t ctx, neo_js_variable_t callee, neo_js_variable_t self,
    uint32_t argc, neo_js_variable_t *argv) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
  neo_js_function_t function = neo_js_variable_to_function(callee);
  neo_js_variable_t result = neo_js_context_construct(
      ctx, ctx->std.async_generator_constructor, 0, NULL);
  neo_js_scope_t current = neo_js_context_set_scope(ctx, scope);
  neo_js_variable_t bind = neo_js_callable_get_bind(ctx, callee);
  if (bind) {
    self = bind;
  } else {
    self = neo_js_context_create_variable(ctx, neo_js_variable_get_chunk(self),
                                          NULL);
  }
  neo_js_variable_t clazz = neo_js_callable_get_class(ctx, callee);
  neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL);
  neo_js_scope_set_variable(scope, arguments, "arguments");
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_context_set_field(ctx, arguments,
                             neo_js_context_create_number(ctx, idx), argv[idx],
                             NULL);
  }
  neo_js_context_set_field(ctx, arguments,
                           neo_js_context_create_string(ctx, "length"),
                           neo_js_context_create_number(ctx, argc), NULL);

  neo_js_context_set_field(
      ctx, arguments,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, "iterator"),
                               NULL),
      neo_js_context_create_cfunction(ctx, "values", neo_js_array_values),
      NULL);

  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const char *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_ref_variable(scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  neo_js_vm_t vm = neo_create_js_vm(ctx, self, clazz, function->address, scope);
  neo_js_variable_t coroutine =
      neo_js_context_create_coroutine(ctx, vm, function->program);
  neo_js_context_set_internal(ctx, result, "[[coroutine]]", coroutine);
  return result;
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
      neo_js_variable_t bind = neo_js_callable_get_bind(ctx, callee);
      if (bind) {
        self = bind;
      } else {
        self = neo_js_context_create_variable(
            ctx, neo_js_variable_get_chunk(self), NULL);
      }
      neo_js_variable_t clazz = neo_js_callable_get_class(ctx, callee);
      neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL);
      neo_js_scope_set_variable(scope, arguments, "arguments");
      for (uint32_t idx = 0; idx < argc; idx++) {
        neo_js_context_set_field(ctx, arguments,
                                 neo_js_context_create_number(ctx, idx),
                                 argv[idx], NULL);
      }
      neo_js_context_set_field(ctx, arguments,
                               neo_js_context_create_string(ctx, "length"),
                               neo_js_context_create_number(ctx, argc), NULL);

      neo_js_context_set_field(
          ctx, arguments,
          neo_js_context_get_field(
              ctx, ctx->std.symbol_constructor,
              neo_js_context_create_string(ctx, "iterator"), NULL),
          neo_js_context_create_cfunction(ctx, "values", neo_js_array_values),
          NULL);
      for (neo_hash_map_node_t it =
               neo_hash_map_get_first(function->callable.closure);
           it != neo_hash_map_get_tail(function->callable.closure);
           it = neo_hash_map_node_next(it)) {
        const char *name = neo_hash_map_node_get_key(it);
        neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
        neo_js_scope_create_ref_variable(scope, hvalue, name);
      }
      neo_js_context_set_scope(ctx, current);
      neo_js_vm_t vm =
          neo_create_js_vm(ctx, self, clazz, function->address, scope);
      neo_js_variable_t result = neo_js_vm_exec(vm, function->program);
      neo_allocator_free(neo_js_context_get_allocator(ctx), vm);
      neo_allocator_free(allocator, scope);
      return result;
    }
  }
}

neo_js_variable_t neo_js_context_call_async_cfunction(neo_js_context_t ctx,
                                                      neo_js_variable_t callee,
                                                      neo_js_variable_t self,
                                                      uint32_t argc,
                                                      neo_js_variable_t *argv) {
  neo_js_async_cfunction_t function =
      neo_js_variable_to_async_cfunction(callee);
  neo_js_variable_t resolver =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_resolver);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
  neo_js_scope_t current = neo_js_context_set_scope(ctx, scope);
  neo_js_variable_t bind = neo_js_callable_get_bind(ctx, callee);
  if (bind) {
    self = bind;
  } else {
    self = neo_js_context_create_variable(ctx, neo_js_variable_get_chunk(self),
                                          NULL);
  }
  neo_js_variable_t *args =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (uint32_t idx = 0; idx < argc; idx++) {
    args[idx] = neo_js_context_create_variable(
        ctx, neo_js_variable_get_chunk(argv[idx]), NULL);
  }
  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const char *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_ref_variable(scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  neo_js_variable_t coroutine = neo_js_context_create_native_coroutine(
      ctx, function->callee, bind, argc, args, scope);
  neo_js_callable_set_closure(ctx, resolver, "#coroutine", coroutine);
  return neo_js_context_construct(ctx, ctx->std.promise_constructor, 1,
                                  &resolver);
}

neo_js_variable_t neo_js_context_call(neo_js_context_t ctx,
                                      neo_js_variable_t callee,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  neo_js_callable_t callable = neo_js_variable_to_callable(callee);
  if (callable && callable->is_class) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Class constructor cannot be invoked without 'new'");
  }
  neo_js_call_type_t current = ctx->call_type;
  ctx->call_type = NEO_JS_FUNCTION_CALL;
  neo_js_variable_t result =
      neo_js_context_simple_call(ctx, callee, self, argc, argv);
  ctx->call_type = current;
  return result;
}

neo_js_variable_t neo_js_context_simple_call(neo_js_context_t ctx,
                                             neo_js_variable_t callee,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {
  for (uint32_t i = 0; i < argc; i++) {
    argv[i] = neo_js_context_clone(ctx, argv[i]);
  }
  if (neo_js_variable_get_type(callee)->kind == NEO_JS_TYPE_CFUNCTION) {
    return neo_js_context_call_cfunction(ctx, callee, self, argc, argv);
  } else if (neo_js_variable_get_type(callee)->kind ==
             NEO_JS_TYPE_ASYNC_CFUNCTION) {
    return neo_js_context_call_async_cfunction(ctx, callee, self, argc, argv);
  } else if (neo_js_variable_get_type(callee)->kind == NEO_JS_TYPE_FUNCTION) {
    return neo_js_context_call_function(ctx, callee, self, argc, argv);
  } else {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "callee is not a function");
  }
}

neo_js_variable_t neo_js_context_construct(neo_js_context_t ctx,
                                           neo_js_variable_t constructor,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(constructor)->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "variable is not a constructor");
  }
  if (neo_js_variable_get_type(constructor)->kind ==
      NEO_JS_TYPE_ASYNC_CFUNCTION) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "variable is not a constructor");
  }
  if (neo_js_variable_get_type(constructor)->kind == NEO_JS_TYPE_FUNCTION) {
    neo_js_function_t fn = neo_js_variable_to_function(constructor);
    if (fn->is_async || fn->is_generator) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0, "variable is not a constructor");
    }
  }
  neo_js_call_type_t current_call_type = ctx->call_type;
  ctx->call_type = NEO_JS_CONSTRUCT_CALL;
  neo_js_scope_t current = neo_js_context_get_scope(ctx);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, "prototype"), NULL);
  neo_js_variable_t object = neo_js_context_create_object(ctx, prototype);
  neo_js_chunk_t hobject = neo_js_variable_get_chunk(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  obj->constructor = neo_js_variable_get_chunk(constructor);
  neo_js_chunk_add_parent(obj->constructor, hobject);
  neo_js_context_def_field(ctx, object,
                           neo_js_context_create_string(ctx, "constructor"),
                           constructor, true, false, true);
  neo_js_variable_t result =
      neo_js_context_simple_call(ctx, constructor, object, argc, argv);
  if (result) {
    if (neo_js_variable_get_type(result)->kind >= NEO_JS_TYPE_OBJECT) {
      object = result;
    } else if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
      object = result;
    }
  }
  hobject = neo_js_variable_get_chunk(object);
  object = neo_js_scope_create_variable(current, hobject, NULL);
  neo_js_context_pop_scope(ctx);
  ctx->call_type = current_call_type;
  return object;
}

const char *neo_js_context_to_error_name(neo_js_context_t ctx,
                                         neo_js_variable_t variable) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  const char *receiver = NULL;
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_SYMBOL) {
    return neo_js_variable_to_symbol(variable)->string;
  } else if (neo_js_variable_get_type(variable)->kind != NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t s = neo_js_context_to_string(ctx, variable);
    receiver = neo_js_variable_to_string(s)->string;
  } else {
    receiver = "#<Object>";
  }
  return receiver;
}

neo_js_variable_t neo_js_context_create_simple_error(neo_js_context_t ctx,
                                                     neo_js_error_type_t type,
                                                     size_t len,
                                                     const char *fmt, ...) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (!len) {
    len = strlen(fmt) + 64;
  }
  char *message =
      neo_allocator_alloc(allocator, sizeof(char) * (len + 1), NULL);
  neo_js_context_defer_free(ctx, message);
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, len, fmt, args);
  va_end(args);
  neo_js_variable_t msg = neo_js_context_create_string(ctx, message);
  neo_js_variable_t value = NULL;
  switch (type) {
  case NEO_JS_ERROR_SYNTAX:
    value = neo_js_context_construct(ctx, ctx->std.syntax_error_constructor, 1,
                                     &msg);
    break;
  case NEO_JS_ERROR_RANGE:
    value = neo_js_context_construct(ctx, ctx->std.range_error_constructor, 1,
                                     &msg);
    break;
  case NEO_JS_ERROR_URI:
    value =
        neo_js_context_construct(ctx, ctx->std.uri_error_constructor, 1, &msg);
    break;
  case NEO_JS_ERROR_TYPE:
    value =
        neo_js_context_construct(ctx, ctx->std.type_error_constructor, 1, &msg);
    break;
  case NEO_JS_ERROR_REFERENCE:
    value = neo_js_context_construct(ctx, ctx->std.reference_error_constructor,
                                     1, &msg);
    break;
  case NEO_JS_ERROR_INTERNAL:
    value = neo_js_context_construct(ctx, ctx->std.internal_error_constructor,
                                     1, &msg);
    break;
  }
  return neo_js_context_create_error(ctx, value);
}

neo_js_variable_t neo_js_context_create_error(neo_js_context_t ctx,
                                              neo_js_variable_t value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_error_t error = neo_create_js_error(allocator, value);

  neo_js_variable_t variable = neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &error->value), NULL);
  neo_js_chunk_t hvariable = neo_js_variable_get_chunk(variable);
  neo_js_chunk_t herror = neo_js_variable_get_chunk(value);
  neo_js_chunk_add_parent(herror, hvariable);
  return variable;
}

neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_undefined_t undefined = neo_create_js_undefined(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &undefined->value), NULL);
}
neo_js_variable_t neo_js_context_create_uninitialize(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_uninitialize_t uninitialize = neo_create_js_uninitialize(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &uninitialize->value), NULL);
}
neo_js_variable_t neo_js_context_create_null(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_null_t null = neo_create_js_null(allocator);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &null->value), NULL);
}

neo_js_variable_t neo_js_context_create_number(neo_js_context_t ctx,
                                               double value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_number_t number = neo_create_js_number(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &number->value), NULL);
}
neo_js_variable_t neo_js_context_create_bigint(neo_js_context_t ctx,
                                               neo_bigint_t value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_bigint_t number = neo_create_js_bigint(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &number->value), NULL);
}

neo_js_variable_t neo_js_context_create_string(neo_js_context_t ctx,
                                               const char *value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_string_t string = neo_create_js_string(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &string->value), NULL);
}

neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t ctx,
                                                bool value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_boolean_t boolean = neo_create_js_boolean(allocator, value);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &boolean->value), NULL);
}

neo_js_variable_t neo_js_context_create_symbol(neo_js_context_t ctx,
                                               const char *description) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_symbol_t symbol = neo_create_js_symbol(allocator, description);
  return neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &symbol->value), NULL);
}
neo_js_variable_t neo_js_context_create_object(neo_js_context_t ctx,
                                               neo_js_variable_t prototype) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_object_t object = neo_create_js_object(allocator);
  neo_js_chunk_t hobject = neo_create_js_chunk(allocator, &object->value);
  if (!prototype) {
    if (ctx->std.object_constructor) {
      prototype = neo_js_context_get_field(
          ctx, ctx->std.object_constructor,
          neo_js_context_create_string(ctx, "prototype"), NULL);
    } else {
      prototype = neo_js_context_create_null(ctx);
    }
  }
  neo_js_chunk_t hproto = neo_js_variable_get_chunk(prototype);
  neo_js_chunk_add_parent(hproto, hobject);
  object->prototype = hproto;
  neo_js_variable_t result = neo_js_context_create_variable(ctx, hobject, NULL);
  if (ctx->std.object_constructor) {
    neo_js_context_def_field(ctx, result,
                             neo_js_context_create_string(ctx, "constructor"),
                             ctx->std.object_constructor, true, false, true);
  }
  return result;
}

neo_js_variable_t neo_js_context_create_array(neo_js_context_t ctx) {
  if (!ctx->std.array_constructor) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_js_array_t arr = neo_create_js_array(allocator);
    neo_js_chunk_t harr = neo_create_js_chunk(allocator, &arr->object.value);
    neo_js_variable_t array = neo_js_context_create_variable(ctx, harr, NULL);
    neo_js_context_def_field(
        ctx, array, neo_js_context_create_string(ctx, "length"),
        neo_js_context_create_number(ctx, 0), false, false, true);
    neo_js_variable_t prototype = neo_js_context_create_null(ctx);
    neo_js_chunk_t hproto = neo_js_variable_get_chunk(prototype);
    neo_js_chunk_add_parent(hproto, harr);
    arr->object.prototype = hproto;
    return array;
  }
  return neo_js_context_construct(ctx, ctx->std.array_constructor, 0, NULL);
}

neo_js_variable_t
neo_js_context_create_interrupt(neo_js_context_t ctx, neo_js_variable_t result,
                                size_t offset, neo_js_interrupt_type_t type) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  neo_js_interrupt_t interrupt =
      neo_create_js_interrupt(allocator, hresult, offset, type);
  neo_js_variable_t variable = neo_js_context_create_variable(
      ctx, neo_create_js_chunk(allocator, &interrupt->value), NULL);
  neo_js_chunk_t hvariable = neo_js_variable_get_chunk(variable);
  neo_js_chunk_add_parent(hresult, hvariable);
  return variable;
}

neo_js_variable_t
neo_js_context_create_cfunction(neo_js_context_t ctx, const char *name,
                                neo_js_cfunction_fn_t cfunction) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_cfunction_t func = neo_create_js_cfunction(allocator, cfunction);
  neo_js_chunk_t hfunction =
      neo_create_js_chunk(allocator, &func->callable.object.value);
  neo_js_chunk_t hproto = NULL;
  if (ctx->std.function_constructor) {
    hproto = neo_js_variable_get_chunk(neo_js_context_get_field(
        ctx, ctx->std.function_constructor,
        neo_js_context_create_string(ctx, "prototype"), NULL));
  } else {
    hproto = neo_js_variable_get_chunk(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_chunk_add_parent(hproto, hfunction);
  if (name) {
    func->callable.name = neo_create_string(allocator, name);
  }
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.function_constructor) {
    neo_js_context_def_field(ctx, result,
                             neo_js_context_create_string(ctx, "constructor"),
                             ctx->std.function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, "prototype"),
      neo_js_context_create_object(ctx, NULL), true, false, true);
  return result;
}

neo_js_variable_t
neo_js_context_create_async_cfunction(neo_js_context_t ctx, const char *name,
                                      neo_js_async_cfunction_fn_t cfunction) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_async_cfunction_t func =
      neo_create_js_async_cfunction(allocator, cfunction);
  neo_js_chunk_t hfunction =
      neo_create_js_chunk(allocator, &func->callable.object.value);
  neo_js_chunk_t hproto = NULL;
  hproto = neo_js_variable_get_chunk(neo_js_context_get_field(
      ctx, ctx->std.function_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL));
  func->callable.object.prototype = hproto;
  neo_js_chunk_add_parent(hproto, hfunction);
  if (name) {
    func->callable.name = neo_create_string(allocator, name);
  }
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.async_function_constructor) {
    neo_js_context_def_field(
        ctx, result, neo_js_context_create_string(ctx, "constructor"),
        ctx->std.async_function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, "prototype"),
      neo_js_context_create_object(ctx, NULL), true, false, true);
  return result;
}

neo_js_variable_t neo_js_context_create_function(neo_js_context_t ctx,
                                                 neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, program);
  neo_js_chunk_t hfunction =
      neo_create_js_chunk(allocator, &func->callable.object.value);
  neo_js_chunk_t hproto = NULL;
  if (ctx->std.function_constructor) {
    hproto = neo_js_variable_get_chunk(neo_js_context_get_field(
        ctx, ctx->std.function_constructor,
        neo_js_context_create_string(ctx, "prototype"), NULL));
  } else {
    hproto = neo_js_variable_get_chunk(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_chunk_add_parent(hproto, hfunction);
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.function_constructor) {
    neo_js_context_def_field(ctx, result,
                             neo_js_context_create_string(ctx, "constructor"),
                             ctx->std.function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, "prototype"),
      neo_js_context_create_object(ctx, NULL), true, false, true);
  return result;
}

neo_js_variable_t
neo_js_context_create_generator_function(neo_js_context_t ctx,
                                         neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, program);
  func->is_generator = true;
  neo_js_chunk_t hfunction =
      neo_create_js_chunk(allocator, &func->callable.object.value);
  neo_js_chunk_t hproto = NULL;
  if (ctx->std.generator_function_constructor) {
    hproto = neo_js_variable_get_chunk(neo_js_context_get_field(
        ctx, ctx->std.generator_function_constructor,
        neo_js_context_create_string(ctx, "prototype"), NULL));
  } else {
    hproto = neo_js_variable_get_chunk(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_chunk_add_parent(hproto, hfunction);
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.generator_function_constructor) {
    neo_js_context_def_field(
        ctx, result, neo_js_context_create_string(ctx, "constructor"),
        ctx->std.generator_function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, "prototype"),
      neo_js_context_create_object(ctx, NULL), true, false, true);
  return result;
}

neo_js_variable_t
neo_js_context_create_async_generator_function(neo_js_context_t ctx,
                                               neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, program);
  func->is_generator = true;
  func->is_async = true;
  neo_js_chunk_t hfunction =
      neo_create_js_chunk(allocator, &func->callable.object.value);
  neo_js_chunk_t hproto = NULL;
  if (ctx->std.async_generator_function_constructor) {
    hproto = neo_js_variable_get_chunk(neo_js_context_get_field(
        ctx, ctx->std.async_generator_function_constructor,
        neo_js_context_create_string(ctx, "prototype"), NULL));
  } else {
    hproto = neo_js_variable_get_chunk(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_chunk_add_parent(hproto, hfunction);
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.async_generator_function_constructor) {
    neo_js_context_def_field(
        ctx, result, neo_js_context_create_string(ctx, "constructor"),
        ctx->std.async_generator_function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, "prototype"),
      neo_js_context_create_object(ctx, NULL), true, false, true);
  return result;
}

neo_js_variable_t neo_js_context_create_async_function(neo_js_context_t ctx,
                                                       neo_program_t program) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_function_t func = neo_create_js_function(allocator, program);
  func->is_async = true;
  neo_js_chunk_t hfunction =
      neo_create_js_chunk(allocator, &func->callable.object.value);
  neo_js_chunk_t hproto = NULL;
  if (ctx->std.async_function_constructor) {
    hproto = neo_js_variable_get_chunk(neo_js_context_get_field(
        ctx, ctx->std.async_function_constructor,
        neo_js_context_create_string(ctx, "prototype"), NULL));
  } else {
    hproto = neo_js_variable_get_chunk(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_chunk_add_parent(hproto, hfunction);
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.async_function_constructor) {
    neo_js_context_def_field(
        ctx, result, neo_js_context_create_string(ctx, "constructor"),
        ctx->std.async_function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, "prototype"),
      neo_js_context_create_object(ctx, NULL), true, false, true);
  return result;
}

neo_js_variable_t neo_js_context_typeof(neo_js_context_t ctx,
                                        neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  const char *type = value->type->typeof_fn(ctx, variable);
  return neo_js_context_create_string(ctx, type);
}

neo_js_variable_t neo_js_context_to_string(neo_js_context_t ctx,
                                           neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_string_fn(ctx, self);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_boolean(neo_js_context_t ctx,
                                            neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_boolean_fn(ctx, self);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_number(neo_js_context_t ctx,
                                           neo_js_variable_t self) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_number_fn(ctx, self);
  neo_js_chunk_t hresult = neo_js_variable_get_chunk(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_to_integer(neo_js_context_t ctx,
                                            neo_js_variable_t variable) {
  variable = neo_js_context_to_number(ctx, variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_ERROR) {
    return variable;
  }
  neo_js_number_t num = neo_js_variable_to_number(variable);
  if (isnan(num->number) || num->number == -0) {
    num->number = 0;
  } else if (!isinf(num->number)) {
    num->number = (int64_t)num->number;
  }
  return variable;
}

neo_js_variable_t neo_js_context_is_equal(neo_js_context_t ctx,
                                          neo_js_variable_t variable,
                                          neo_js_variable_t another) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_type_t lefttype = neo_js_variable_get_type(variable);
  neo_js_type_t righttype = neo_js_variable_get_type(another);
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  if (lefttype->kind != righttype->kind) {
    if (lefttype->kind == NEO_JS_TYPE_NULL ||
        lefttype->kind == NEO_JS_TYPE_UNDEFINED) {
      if (righttype->kind == NEO_JS_TYPE_NULL ||
          righttype->kind == NEO_JS_TYPE_UNDEFINED) {
        return neo_js_context_create_boolean(ctx, true);
      }
    }
    if (righttype->kind == NEO_JS_TYPE_NULL ||
        righttype->kind == NEO_JS_TYPE_UNDEFINED) {
      if (righttype->kind == NEO_JS_TYPE_NULL ||
          righttype->kind == NEO_JS_TYPE_UNDEFINED) {
        return neo_js_context_create_boolean(ctx, true);
      }
    }
    if (lefttype->kind >= NEO_JS_TYPE_OBJECT &&
        righttype->kind < NEO_JS_TYPE_OBJECT) {
      left = neo_js_context_to_primitive(ctx, left, "default");
      if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
        return left;
      }
      lefttype = neo_js_variable_get_type(left);
    }
    if (righttype->kind >= NEO_JS_TYPE_OBJECT &&
        lefttype->kind < NEO_JS_TYPE_OBJECT) {
      right = neo_js_context_to_primitive(ctx, right, "default");
      if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
        return right;
      }
      righttype = neo_js_variable_get_type(right);
    }
    if (lefttype->kind != righttype->kind) {
      if (lefttype->kind == NEO_JS_TYPE_SYMBOL ||
          righttype->kind == NEO_JS_TYPE_SYMBOL) {
        return neo_js_context_create_boolean(ctx, false);
      }
    }
    if (lefttype->kind == NEO_JS_TYPE_BOOLEAN) {
      left = neo_js_context_to_number(ctx, left);
      return neo_js_context_is_equal(ctx, left, right);
    }
    if (righttype->kind == NEO_JS_TYPE_BOOLEAN) {
      right = neo_js_context_to_number(ctx, right);
      return neo_js_context_is_equal(ctx, left, right);
    }

    if (lefttype->kind == NEO_JS_TYPE_BIGINT &&
        righttype->kind == NEO_JS_TYPE_NUMBER) {
      double r = neo_js_variable_to_number(right)->number;
      if (isnan(r) || isinf(r)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t b = neo_number_to_bigint(allocator, r);
      neo_bigint_t a = neo_js_variable_to_bigint(left)->bigint;
      bool is_equ = neo_bigint_is_equal(a, b);
      neo_allocator_free(allocator, b);
      return neo_js_context_create_boolean(ctx, is_equ);
    }

    if (righttype->kind == NEO_JS_TYPE_BIGINT &&
        lefttype->kind == NEO_JS_TYPE_NUMBER) {
      double r = neo_js_variable_to_number(left)->number;
      if (isnan(r) || isinf(r)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t b = neo_number_to_bigint(allocator, r);
      neo_bigint_t a = neo_js_variable_to_bigint(right)->bigint;
      bool is_equ = neo_bigint_is_equal(a, b);
      neo_allocator_free(allocator, b);
      return neo_js_context_create_boolean(ctx, is_equ);
    }

    if (lefttype->kind == NEO_JS_TYPE_BIGINT &&
        righttype->kind == NEO_JS_TYPE_STRING) {
      const char *r = neo_js_variable_to_string(right)->string;
      neo_bigint_t b = neo_string_to_bigint(allocator, r);
      if (!b) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t a = neo_js_variable_to_bigint(left)->bigint;
      bool is_equ = neo_bigint_is_equal(a, b);
      neo_allocator_free(allocator, b);
      return neo_js_context_create_boolean(ctx, is_equ);
    }

    if (righttype->kind == NEO_JS_TYPE_BIGINT &&
        lefttype->kind == NEO_JS_TYPE_STRING) {
      const char *r = neo_js_variable_to_string(left)->string;
      neo_bigint_t b = neo_string_to_bigint(allocator, r);
      if (!b) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t a = neo_js_variable_to_bigint(right)->bigint;
      bool is_equ = neo_bigint_is_equal(a, b);
      neo_allocator_free(allocator, b);
      return neo_js_context_create_boolean(ctx, is_equ);
    }

    if (lefttype->kind == NEO_JS_TYPE_STRING &&
        righttype->kind == NEO_JS_TYPE_NUMBER) {
      left = neo_js_context_to_number(ctx, left);
      lefttype = neo_js_variable_get_type(left);
    } else if (lefttype->kind == NEO_JS_TYPE_NUMBER &&
               righttype->kind == NEO_JS_TYPE_STRING) {
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
  left = neo_js_context_to_primitive(ctx, left, "number");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "number");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING &&
      righttype->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = strcmp(lstring->string, rstring->string);
    return neo_js_context_create_boolean(ctx, res > 0);
  }
  if (lefttype->kind != NEO_JS_TYPE_NUMBER &&
      lefttype->kind != NEO_JS_TYPE_BIGINT) {
    left = neo_js_context_to_number(ctx, left);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return left;
    }
  }
  if (righttype->kind != NEO_JS_TYPE_NUMBER &&
      righttype->kind != NEO_JS_TYPE_BIGINT) {
    right = neo_js_context_to_number(ctx, right);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return right;
    }
  }
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    if (lefttype->kind == NEO_JS_TYPE_NUMBER) {
      neo_js_number_t num = neo_js_variable_to_number(left);
      if (isnan(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number) && num->number > 0) {
        return neo_js_context_create_boolean(ctx, true);
      }
      if (isinf(num->number) && num->number < 0) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
      bool res = neo_bigint_is_greater(bigint, b->bigint);
      neo_allocator_free(allocator, bigint);
      return neo_js_context_create_boolean(ctx, res);
    }
    if (righttype->kind == NEO_JS_TYPE_NUMBER) {
      neo_js_number_t num = neo_js_variable_to_number(right);
      if (isnan(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number) && num->number > 0) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number) && num->number < 0) {
        return neo_js_context_create_boolean(ctx, true);
      }
      neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
      bool res = neo_bigint_is_greater(a->bigint, bigint);
      neo_allocator_free(allocator, bigint);
      return neo_js_context_create_boolean(ctx, res);
    }
    return neo_js_context_create_boolean(
        ctx, neo_bigint_is_greater(a->bigint, b->bigint));
  } else {
    neo_js_number_t lnum = neo_js_variable_to_number(left);
    neo_js_number_t rnum = neo_js_variable_to_number(right);
    return neo_js_context_create_boolean(ctx, lnum->number > rnum->number);
  }
}

neo_js_variable_t neo_js_context_is_lt(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, "number");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "number");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING &&
      righttype->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = strcmp(lstring->string, rstring->string);
    return neo_js_context_create_boolean(ctx, res < 0);
  }
  if (lefttype->kind != NEO_JS_TYPE_NUMBER &&
      lefttype->kind != NEO_JS_TYPE_BIGINT) {
    left = neo_js_context_to_number(ctx, left);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return left;
    }
  }
  if (righttype->kind != NEO_JS_TYPE_NUMBER &&
      righttype->kind != NEO_JS_TYPE_BIGINT) {
    right = neo_js_context_to_number(ctx, right);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return right;
    }
  }
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    if (lefttype->kind == NEO_JS_TYPE_NUMBER) {
      neo_js_number_t num = neo_js_variable_to_number(left);
      if (isnan(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number) && num->number > 0) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number) && num->number < 0) {
        return neo_js_context_create_boolean(ctx, true);
      }
      neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
      bool res = neo_bigint_is_less(bigint, b->bigint);
      neo_allocator_free(allocator, bigint);
      return neo_js_context_create_boolean(ctx, res);
    }
    if (righttype->kind == NEO_JS_TYPE_NUMBER) {
      neo_js_number_t num = neo_js_variable_to_number(right);
      if (isnan(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number) && num->number > 0) {
        return neo_js_context_create_boolean(ctx, true);
      }
      if (isinf(num->number) && num->number < 0) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
      bool res = neo_bigint_is_less(a->bigint, bigint);
      neo_allocator_free(allocator, bigint);
      return neo_js_context_create_boolean(ctx, !res);
    }
    return neo_js_context_create_boolean(
        ctx, neo_bigint_is_less(a->bigint, b->bigint));
  } else {
    neo_js_number_t lnum = neo_js_variable_to_number(left);
    neo_js_number_t rnum = neo_js_variable_to_number(right);
    return neo_js_context_create_boolean(ctx, lnum->number < rnum->number);
  }
}

neo_js_variable_t neo_js_context_is_ge(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, "number");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "number");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING &&
      righttype->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = strcmp(lstring->string, rstring->string);
    return neo_js_context_create_boolean(ctx, res >= 0);
  }
  if (lefttype->kind != NEO_JS_TYPE_NUMBER &&
      lefttype->kind != NEO_JS_TYPE_BIGINT) {
    left = neo_js_context_to_number(ctx, left);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return left;
    }
  }
  if (righttype->kind != NEO_JS_TYPE_NUMBER &&
      righttype->kind != NEO_JS_TYPE_BIGINT) {
    right = neo_js_context_to_number(ctx, right);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return right;
    }
  }
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    if (lefttype->kind == NEO_JS_TYPE_NUMBER) {
      neo_js_number_t num = neo_js_variable_to_number(left);
      if (isnan(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
      bool res = neo_bigint_is_greater_or_equal(bigint, b->bigint);
      neo_allocator_free(allocator, bigint);
      return neo_js_context_create_boolean(ctx, res);
    }
    if (righttype->kind == NEO_JS_TYPE_NUMBER) {
      neo_js_number_t num = neo_js_variable_to_number(right);
      if (isnan(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
      bool res = neo_bigint_is_greater_or_equal(a->bigint, bigint);
      neo_allocator_free(allocator, bigint);
      return neo_js_context_create_boolean(ctx, res);
    }
    return neo_js_context_create_boolean(
        ctx, neo_bigint_is_greater_or_equal(a->bigint, b->bigint));
  } else {
    neo_js_number_t lnum = neo_js_variable_to_number(left);
    neo_js_number_t rnum = neo_js_variable_to_number(right);
    return neo_js_context_create_boolean(ctx, lnum->number >= rnum->number);
  }
}

neo_js_variable_t neo_js_context_is_le(neo_js_context_t ctx,
                                       neo_js_variable_t variable,
                                       neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, "number");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "number");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING &&
      righttype->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = strcmp(lstring->string, rstring->string);
    return neo_js_context_create_boolean(ctx, res <= 0);
  }
  if (lefttype->kind != NEO_JS_TYPE_NUMBER &&
      lefttype->kind != NEO_JS_TYPE_BIGINT) {
    left = neo_js_context_to_number(ctx, left);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return left;
    }
  }
  if (righttype->kind != NEO_JS_TYPE_NUMBER &&
      righttype->kind != NEO_JS_TYPE_BIGINT) {
    right = neo_js_context_to_number(ctx, right);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return right;
    }
  }
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    if (lefttype->kind == NEO_JS_TYPE_NUMBER) {
      neo_js_number_t num = neo_js_variable_to_number(left);
      if (isnan(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
      bool res = neo_bigint_is_greater_or_equal(bigint, b->bigint);
      neo_allocator_free(allocator, bigint);
      return neo_js_context_create_boolean(ctx, res);
    }
    if (righttype->kind == NEO_JS_TYPE_NUMBER) {
      neo_js_number_t num = neo_js_variable_to_number(right);
      if (isnan(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      if (isinf(num->number)) {
        return neo_js_context_create_boolean(ctx, false);
      }
      neo_bigint_t bigint = neo_number_to_bigint(allocator, num->number);
      bool res = neo_bigint_is_less_or_equal(a->bigint, bigint);
      neo_allocator_free(allocator, bigint);
      return neo_js_context_create_boolean(ctx, res);
    }
    return neo_js_context_create_boolean(
        ctx, neo_bigint_is_less_or_equal(a->bigint, b->bigint));
  } else {
    neo_js_number_t lnum = neo_js_variable_to_number(left);
    neo_js_number_t rnum = neo_js_variable_to_number(right);
    return neo_js_context_create_boolean(ctx, lnum->number <= rnum->number);
  }
}

neo_js_variable_t neo_js_context_add(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING ||
      righttype->kind == NEO_JS_TYPE_STRING) {
    left = neo_js_context_to_string(ctx, left);
    if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
      return left;
    }
    right = neo_js_context_to_string(ctx, right);
    if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
      return right;
    }
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    size_t len = strlen(lstring->string);
    len += strlen(rstring->string);
    len++;
    char *str = neo_allocator_alloc(allocator, sizeof(char) * len, NULL);
    snprintf(str, len, "%s%s", lstring->string, rstring->string);
    neo_js_variable_t result = neo_js_context_create_string(ctx, str);
    neo_allocator_free(allocator, str);
    return result;
  }
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    return neo_js_context_create_bigint(ctx,
                                        neo_bigint_add(a->bigint, b->bigint));
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    return neo_js_context_create_bigint(ctx,
                                        neo_bigint_sub(a->bigint, b->bigint));
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    return neo_js_context_create_bigint(ctx,
                                        neo_bigint_mul(a->bigint, b->bigint));
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_div(a->bigint, b->bigint);
    if (!res) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE, 0,
                                                "Division by zero");
    }
    return neo_js_context_create_bigint(ctx, res);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_div(a->bigint, b->bigint);
    if (!res) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE, 0,
                                                "Division by zero");
    }
    return neo_js_context_create_bigint(ctx, res);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  uint64_t count = (uint64_t)lnum->number / rnum->number;
  double value = lnum->number - rnum->number * count;
  return neo_js_context_create_number(ctx, value);
}

neo_js_variable_t neo_js_context_pow(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_pow(a->bigint, b->bigint);
    return neo_js_context_create_bigint(ctx, res);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  neo_js_number_t rnum = neo_js_variable_to_number(right);
  return neo_js_context_create_number(ctx, pow(lnum->number, rnum->number));
}

neo_js_variable_t neo_js_context_not(neo_js_context_t ctx,
                                     neo_js_variable_t variable) {
  neo_js_variable_t left = variable;
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT) {
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_bigint_t res = neo_bigint_not(a->bigint);
    return neo_js_context_create_bigint(ctx, res);
  }
  neo_js_number_t lnum = neo_js_variable_to_number(left);
  int32_t i32 = lnum->number;
  return neo_js_context_create_number(ctx, ~i32);
}

neo_js_variable_t neo_js_context_instance_of(neo_js_context_t ctx,
                                             neo_js_variable_t variable,
                                             neo_js_variable_t constructor) {
  neo_js_type_t type = neo_js_variable_get_type(variable);
  if (type->kind != NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_variable_t hasInstance = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, "hasInstance"), NULL);
  if (hasInstance &&
      neo_js_variable_get_type(hasInstance)->kind == NEO_JS_TYPE_CFUNCTION) {
    return neo_js_context_call(ctx, hasInstance, constructor, 1, &variable);
  }
  neo_js_object_t object = neo_js_variable_to_object(variable);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, "prototype"), NULL);
  while (object) {
    if (object == neo_js_variable_to_object(prototype)) {
      return neo_js_context_create_boolean(ctx, true);
    }
    object = neo_js_value_to_object(neo_js_chunk_get_value(object->prototype));
  }
  return neo_js_context_create_boolean(ctx, false);
}

neo_js_variable_t neo_js_context_in(neo_js_context_t ctx,
                                    neo_js_variable_t field,
                                    neo_js_variable_t variable) {
  neo_js_object_property_t prop =
      neo_js_object_get_property(ctx, variable, field);
  if (!prop) {
    return neo_js_context_create_boolean(ctx, false);
  }
  return neo_js_context_create_boolean(ctx, true);
}

neo_js_variable_t neo_js_context_and(neo_js_context_t ctx,
                                     neo_js_variable_t variable,
                                     neo_js_variable_t another) {
  neo_js_variable_t left = variable;
  neo_js_variable_t right = another;
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_and(a->bigint, b->bigint);
    return neo_js_context_create_bigint(ctx, res);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_or(a->bigint, b->bigint);
    return neo_js_context_create_bigint(ctx, res);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_xor(a->bigint, b->bigint);
    return neo_js_context_create_bigint(ctx, res);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_shr(a->bigint, b->bigint);
    return neo_js_context_create_bigint(ctx, res);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_shl(a->bigint, b->bigint);
    return neo_js_context_create_bigint(ctx, res);
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, "default");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, 0,
          "Cannot mix BigInt and other types, use explicit conversions");
    }
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "BigInts have no unsigned right shift, use >> instead");
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_number(ctx, right);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
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
                                     neo_js_variable_t left) {
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_BIGINT) {
    neo_js_bigint_t bigint = neo_js_variable_to_bigint(left);
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_bigint_t add_arg = neo_number_to_bigint(allocator, 1);
    neo_bigint_t res = neo_bigint_add(bigint->bigint, add_arg);
    neo_allocator_free(allocator, add_arg);
    neo_allocator_free(allocator, bigint->bigint);
    bigint->bigint = res;
    return left;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  neo_js_number_t num = neo_js_variable_to_number(left);
  num->number += 1;
  return left;
}

neo_js_variable_t neo_js_context_dec(neo_js_context_t ctx,
                                     neo_js_variable_t left) {
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_BIGINT) {
    neo_js_bigint_t bigint = neo_js_variable_to_bigint(left);
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    neo_bigint_t add_arg = neo_number_to_bigint(allocator, 1);
    neo_bigint_t res = neo_bigint_sub(bigint->bigint, add_arg);
    neo_allocator_free(allocator, add_arg);
    neo_allocator_free(allocator, bigint->bigint);
    bigint->bigint = res;
    return left;
  }
  left = neo_js_context_to_number(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  neo_js_number_t num = neo_js_variable_to_number(left);
  num->number += 1;
  return left;
}

neo_js_variable_t neo_js_context_logical_not(neo_js_context_t ctx,
                                             neo_js_variable_t variable) {
  neo_js_variable_t left = variable;
  left = neo_js_context_to_primitive(ctx, left, "default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  left = neo_js_context_to_boolean(ctx, left);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  neo_js_boolean_t boolean = neo_js_variable_to_boolean(left);
  return neo_js_context_create_boolean(ctx, !boolean->boolean);
}

neo_js_variable_t neo_js_context_plus(neo_js_context_t ctx,
                                      neo_js_variable_t variable) {
  variable = neo_js_context_to_primitive(ctx, variable, "default");
  NEO_JS_TRY_AND_THROW(variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_BIGINT ||
      neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_NUMBER) {
    return variable;
  }
  variable = neo_js_context_to_number(ctx, variable);
  NEO_JS_TRY_AND_THROW(variable);
  return variable;
}

neo_js_variable_t neo_js_context_neg(neo_js_context_t ctx,
                                     neo_js_variable_t variable) {
  variable = neo_js_context_to_primitive(ctx, variable, "default");
  NEO_JS_TRY_AND_THROW(variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_BIGINT) {
    neo_bigint_t bigint = neo_js_variable_to_bigint(variable)->bigint;
    bigint = neo_bigint_neg(bigint);
    return neo_js_context_create_bigint(ctx, bigint);
  }
  variable = neo_js_context_to_number(ctx, variable);
  NEO_JS_TRY_AND_THROW(variable);
  return neo_js_context_create_number(
      ctx, -neo_js_variable_to_number(variable)->number);
}

neo_js_variable_t neo_js_context_concat(neo_js_context_t ctx,
                                        neo_js_variable_t variable,
                                        neo_js_variable_t another) {
  neo_js_variable_t left = neo_js_context_to_string(ctx, variable);
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  neo_js_variable_t right = neo_js_context_to_string(ctx, another);
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_string_t lstring = neo_js_variable_to_string(left);
  neo_js_string_t rstring = neo_js_variable_to_string(right);
  size_t len = strlen(lstring->string);
  len += strlen(rstring->string);
  len += 1;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *str = neo_allocator_alloc(allocator, len * sizeof(char), NULL);
  snprintf(str, len, "%s%s", lstring->string, rstring->string);
  neo_js_variable_t result = neo_js_context_create_string(ctx, str);
  neo_allocator_free(allocator, str);
  return result;
}

static neo_js_variable_t neo_js_context_eval_resolver(neo_js_context_t ctx,
                                                      neo_js_variable_t self,
                                                      uint32_t argc,
                                                      neo_js_variable_t *argv) {
  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_list_t stack = co_ctx->vm->stack;
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(stack));
  if (neo_js_context_is_thenable(ctx, value)) {
    neo_list_pop(stack);
    neo_js_variable_t resolve = argv[0];
    neo_js_variable_t reject = argv[1];
    neo_js_variable_t task =
        neo_js_context_create_cfunction(ctx, NULL, &neo_js_awaiter_task);
    neo_js_variable_t on_fulfilled =
        neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_on_fulfilled);
    neo_js_variable_t on_rejected =
        neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_on_rejected);
    neo_js_callable_set_closure(ctx, task, "#coroutine", coroutine);
    neo_js_callable_set_closure(ctx, task, "#resolve", resolve);
    neo_js_callable_set_closure(ctx, task, "#reject", reject);
    neo_js_callable_set_closure(ctx, task, "#onFulfilled", on_fulfilled);
    neo_js_callable_set_closure(ctx, task, "#onRejected", on_rejected);
    neo_js_callable_set_closure(ctx, task, "#task", task);

    neo_js_callable_set_closure(ctx, on_fulfilled, "#task", task);
    neo_js_callable_set_closure(ctx, on_fulfilled, "#coroutine", coroutine);

    neo_js_callable_set_closure(ctx, on_rejected, "#task", task);
    neo_js_callable_set_closure(ctx, on_rejected, "#coroutine", coroutine);

    neo_js_variable_t then = neo_js_context_get_field(
        ctx, value, neo_js_context_create_string(ctx, "then"), NULL);

    neo_js_variable_t args[] = {on_fulfilled, on_rejected};

    return neo_js_context_call(ctx, then, value, 2, args);
  } else {
    return neo_js_awaiter_resolver(ctx, self, argc, argv);
  }
}

static neo_js_variable_t
neo_js_context_eval_backend_resolver(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  neo_js_variable_t coroutine = neo_js_context_load_variable(ctx, "#coroutine");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_list_t stack = co_ctx->vm->stack;
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(stack));
  if (neo_js_context_is_thenable(ctx, value)) {
    neo_list_pop(stack);
    neo_js_variable_t resolve = argv[0];
    neo_js_variable_t reject = argv[1];
    neo_js_variable_t task =
        neo_js_context_create_cfunction(ctx, NULL, &neo_js_awaiter_task);
    neo_js_variable_t on_rejected =
        neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_on_rejected);
    neo_js_callable_set_closure(ctx, task, "#coroutine", coroutine);
    neo_js_callable_set_closure(ctx, task, "#resolve", resolve);
    neo_js_callable_set_closure(ctx, task, "#reject", reject);
    neo_js_callable_set_closure(ctx, task, "#onFulfilled", task);
    neo_js_callable_set_closure(ctx, task, "#onRejected", on_rejected);
    neo_js_callable_set_closure(ctx, task, "#task", task);

    neo_js_callable_set_closure(ctx, on_rejected, "#task", task);
    neo_js_callable_set_closure(ctx, on_rejected, "#coroutine", coroutine);

    neo_js_variable_t then = neo_js_context_get_field(
        ctx, value, neo_js_context_create_string(ctx, "then"), NULL);

    neo_js_variable_t args[] = {task, on_rejected};

    return neo_js_context_call(ctx, then, value, 2, args);
  } else {
    return neo_js_awaiter_resolver(ctx, self, argc, argv);
  }
}

neo_js_variable_t neo_js_context_create_compile_error(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_error_t err = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
  const char *message = neo_error_get_message(err);
  neo_js_variable_t error =
      neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX, 0, message);
  neo_allocator_free(allocator, err);
  return error;
}

void neo_js_context_create_module(neo_js_context_t ctx, const char *name,
                                  neo_js_variable_t module) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_chunk_t hmodule = neo_js_variable_get_chunk(module);
  neo_js_chunk_add_parent(hmodule, neo_js_scope_get_root_chunk(ctx->root));
  neo_hash_map_set(ctx->modules, neo_create_string(allocator, name), hmodule,
                   NULL, NULL);
}

neo_js_variable_t neo_js_context_get_module(neo_js_context_t ctx,
                                            const char *name) {
  neo_js_chunk_t hmodule = neo_hash_map_get(ctx->modules, name, NULL, NULL);
  if (!hmodule) {
    size_t len = 64 + strlen(name);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_REFERENCE, len,
                                              "Cannot find module '%s'", name);
  }
  return neo_js_context_create_variable(ctx, hmodule, NULL);
}

bool neo_js_context_has_module(neo_js_context_t ctx, const char *name) {
  neo_js_chunk_t hmodule = neo_hash_map_get(ctx->modules, name, NULL, NULL);
  if (!hmodule) {
    return false;
  }
  return true;
}

neo_js_variable_t neo_js_context_eval(neo_js_context_t ctx, const char *file,
                                      const char *source) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_path_t path = neo_create_path(allocator, file);
  path = neo_path_absolute(path);
  char *filepath = neo_path_to_string(path);
  const char *path_ext_name = neo_path_extname(path);
  char *ext_name = NULL;
  if (path_ext_name) {
    ext_name = neo_string_to_lower(allocator, path_ext_name);
  }
  neo_allocator_free(allocator, path);
  if (ext_name && strcmp(ext_name, ".json") == 0) {
    neo_allocator_free(allocator, ext_name);
    neo_position_t position = {0, 0, source};
    neo_js_variable_t def =
        neo_js_json_read_variable(ctx, &position, NULL, filepath);
    NEO_JS_TRY_AND_THROW(def);
    neo_js_variable_t module =
        neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));
    neo_js_chunk_t hmodule = neo_js_variable_get_chunk(module);
    neo_js_chunk_add_parent(hmodule, neo_js_scope_get_root_chunk(ctx->root));
    neo_js_context_set_field(
        ctx, module, neo_js_context_create_string(ctx, "default"), def, NULL);
    neo_hash_map_set(ctx->modules, neo_create_string(allocator, filepath),
                     hmodule, NULL, NULL);
    neo_allocator_free(allocator, filepath);
    return module;
  }
  neo_allocator_free(allocator, ext_name);
  neo_program_t program = neo_js_runtime_get_program(ctx->runtime, filepath);
  if (!program) {
    neo_js_variable_t module =
        neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));

    neo_js_chunk_t hmodule = neo_js_variable_get_chunk(module);
    neo_js_chunk_add_parent(hmodule, neo_js_scope_get_root_chunk(ctx->root));
    neo_hash_map_set(ctx->modules, neo_create_string(allocator, filepath),
                     hmodule, NULL, NULL);

    neo_ast_node_t root = TRY(neo_ast_parse_code(allocator, filepath, source)) {
      neo_allocator_free(allocator, filepath);
      return neo_js_context_create_compile_error(ctx);
    };

    program = TRY(neo_ast_write_node(allocator, filepath, root)) {
      neo_allocator_free(allocator, root);
      neo_allocator_free(allocator, filepath);
      return neo_js_context_create_compile_error(ctx);
    }
    neo_allocator_free(allocator, root);
    neo_js_runtime_set_program(ctx->runtime, filepath, program);
    char *asmpath = neo_create_string(allocator, filepath);
    size_t max = strlen(asmpath) + 1;
    asmpath = neo_string_concat(allocator, asmpath, &max, ".asm");
    FILE *fp = fopen(asmpath, "w");
    TRY(neo_program_write(allocator, fp, program)) {
      neo_allocator_free(allocator, root);
      neo_allocator_free(allocator, filepath);
      return neo_js_context_create_compile_error(ctx);
    };
    fclose(fp);
    neo_allocator_free(allocator, asmpath);
    neo_allocator_free(allocator, filepath);
  }
  neo_js_scope_t scope = neo_create_js_scope(allocator, ctx->root);
  neo_js_vm_t vm = neo_create_js_vm(ctx, neo_js_context_create_undefined(ctx),
                                    NULL, 0, scope);
  neo_js_variable_t result = neo_js_vm_exec(vm, program);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_INTERRUPT) {
    neo_js_interrupt_t interrupt = neo_js_variable_to_interrupt(result);
    result = neo_js_context_create_variable(ctx, interrupt->result, NULL);
    vm->offset = interrupt->offset;
    neo_js_variable_t coroutine =
        neo_js_context_create_coroutine(ctx, vm, program);
    neo_js_variable_t resolver = NULL;
    neo_list_push(vm->stack, result);
    if (interrupt->type == NEO_JS_INTERRUPT_BACKEND_AWAIT) {
      resolver = neo_js_context_create_cfunction(
          ctx, NULL, neo_js_context_eval_backend_resolver);
    } else {
      resolver = neo_js_context_create_cfunction(ctx, NULL,
                                                 neo_js_context_eval_resolver);
    }
    neo_js_callable_set_closure(ctx, resolver, "#coroutine", coroutine);
    return neo_js_context_construct(ctx, ctx->std.promise_constructor, 1,
                                    &resolver);
  } else {
    neo_allocator_free(allocator, scope);
    neo_allocator_free(allocator, vm);
    return result;
  }
}
neo_js_variable_t neo_js_context_assert(neo_js_context_t ctx, const char *type,
                                        const char *value, const char *file) {
  return ctx->assert_fn(ctx, type, value, file);
}

neo_js_assert_fn_t neo_js_context_set_assert_fn(neo_js_context_t ctx,
                                                neo_js_assert_fn_t assert_fn) {
  neo_js_assert_fn_t current = ctx->assert_fn;
  ctx->assert_fn = assert_fn;
  return current;
}

void neo_js_context_enable(neo_js_context_t ctx, const char *feature) {
  neo_js_feature_t feat = neo_hash_map_get(ctx->features, feature, NULL, NULL);
  if (feat && feat->enable_fn) {
    feat->enable_fn(ctx, feature);
  }
}

void neo_js_context_disable(neo_js_context_t ctx, const char *feature) {
  neo_js_feature_t feat = neo_hash_map_get(ctx->features, feature, NULL, NULL);
  if (feat && feat->disable_fn) {
    feat->disable_fn(ctx, feature);
  }
}

void neo_js_context_set_feature(neo_js_context_t ctx, const char *feature,
                                neo_js_feature_fn_t enable_fn,
                                neo_js_feature_fn_t disable_fn) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_feature_t feat =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_feature_t), NULL);
  feat->enable_fn = enable_fn;
  feat->disable_fn = disable_fn;
  neo_hash_map_set(ctx->features, neo_create_string(allocator, feature), feat,
                   NULL, NULL);
}
neo_js_call_type_t neo_js_context_get_call_type(neo_js_context_t ctx) {
  return ctx->call_type;
}