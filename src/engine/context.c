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
#include "core/unicode.h"
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
#include "engine/handle.h"
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
#include "engine/std/symbol.h"
#include "engine/std/syntax_error.h"
#include "engine/std/type_error.h"
#include "engine/std/uri_error.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/vm.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
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

static void neo_js_context_init_std_symbol(neo_js_context_t ctx) {
  neo_js_context_push_scope(ctx);
  neo_js_variable_t async_iterator =
      neo_js_context_create_symbol(ctx, L"asyncIterator");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"asyncIterator"),
                           async_iterator, true, false, true);

  neo_js_variable_t async_dispose =
      neo_js_context_create_symbol(ctx, L"asyncDispose");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"asyncDispose"),
                           async_dispose, true, false, true);

  neo_js_variable_t dispose = neo_js_context_create_symbol(ctx, L"dispose");
  neo_js_context_def_field(ctx, ctx->std.symbol_constructor,
                           neo_js_context_create_string(ctx, L"dispose"),
                           dispose, true, false, true);

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

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

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

  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"assign"),
      neo_js_context_create_cfunction(ctx, L"assign", neo_js_object_assign),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"defineProperties"),
      neo_js_context_create_cfunction(ctx, L"defineProperties",
                                      neo_js_object_define_properties),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"defineProperty"),
      neo_js_context_create_cfunction(ctx, L"defineProperty",
                                      neo_js_object_define_property),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"entries"),
      neo_js_context_create_cfunction(ctx, L"entries", neo_js_object_entries),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"freeze"),
      neo_js_context_create_cfunction(ctx, L"freeze", neo_js_object_freeze),
      true, false, true);
  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, L"fromEntries"),
                           neo_js_context_create_cfunction(
                               ctx, L"fromEntries", neo_js_object_from_entries),
                           true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"getOwnPropertyDescriptor"),
      neo_js_context_create_cfunction(
          ctx, L"getOwnPropertyDescriptor",
          neo_js_object_get_own_property_descriptor),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"getOwnPropertyDescriptors"),
      neo_js_context_create_cfunction(
          ctx, L"getOwnPropertyDescriptors",
          neo_js_object_get_own_property_descriptors),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"getOwnPropertyNames"),
      neo_js_context_create_cfunction(ctx, L"getOwnPropertyNames",
                                      neo_js_object_get_own_property_names),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"getOwnPropertySymbols"),
      neo_js_context_create_cfunction(ctx, L"getOwnPropertySymbols",
                                      neo_js_object_get_own_property_symbols),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"getPrototypeOf"),
      neo_js_context_create_cfunction(ctx, L"getPrototypeOf",
                                      neo_js_object_get_prototype_of),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"groupBy"),
      neo_js_context_create_cfunction(ctx, L"groupBy", neo_js_object_group_by),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"hasOwn"),
      neo_js_context_create_cfunction(ctx, L"hasOwn", neo_js_object_has_own),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"is"),
      neo_js_context_create_cfunction(ctx, L"is", neo_js_object_is), true,
      false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"isExtensible"),
      neo_js_context_create_cfunction(ctx, L"isExtensible",
                                      neo_js_object_is_extensible),
      true, false, true);
  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, L"isFrozen"),
                           neo_js_context_create_cfunction(
                               ctx, L"isFrozen", neo_js_object_is_frozen),
                           true, false, true);
  neo_js_context_def_field(ctx, ctx->std.object_constructor,
                           neo_js_context_create_string(ctx, L"isSealed"),
                           neo_js_context_create_cfunction(
                               ctx, L"isSealed", neo_js_object_is_sealed),
                           true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"preventExtensions"),
      neo_js_context_create_cfunction(ctx, L"preventExtensions",
                                      neo_js_object_prevent_extensions),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"seal"),
      neo_js_context_create_cfunction(ctx, L"seal", neo_js_object_seal), true,
      false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"setPrototypeOf"),
      neo_js_context_create_cfunction(ctx, L"setPrototypeOf",
                                      neo_js_object_set_prototype_of),
      true, false, true);
  neo_js_context_def_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"values"),
      neo_js_context_create_cfunction(ctx, L"values", neo_js_object_values),
      true, false, true);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.object_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);
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
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.function_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           neo_js_context_create_cfunction(
                               ctx, L"toString", neo_js_function_to_string),
                           true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"call"),
      neo_js_context_create_cfunction(ctx, L"call", neo_js_function_call), true,
      false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"apply"),
      neo_js_context_create_cfunction(ctx, L"apply", neo_js_function_apply),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"bind"),
      neo_js_context_create_cfunction(ctx, L"bind", neo_js_function_bind), true,
      false, true);
}

static void neo_js_context_init_std_array(neo_js_context_t ctx) {

  neo_js_variable_t from =
      neo_js_context_create_cfunction(ctx, L"from", neo_js_array_from);
  neo_js_context_def_field(ctx, ctx->std.array_constructor,
                           neo_js_context_create_string(ctx, L"from"), from,
                           true, false, true);

  neo_js_variable_t from_async = neo_js_context_create_async_cfunction(
      ctx, L"fromAsync", neo_js_array_from_async);
  neo_js_context_def_field(ctx, ctx->std.array_constructor,
                           neo_js_context_create_string(ctx, L"fromAsync"),
                           from_async, true, false, true);

  neo_js_variable_t is_array =
      neo_js_context_create_cfunction(ctx, L"isArray", neo_js_array_is_array);
  neo_js_context_def_field(ctx, ctx->std.array_constructor,
                           neo_js_context_create_string(ctx, L"isArray"),
                           is_array, true, false, true);

  neo_js_variable_t of =
      neo_js_context_create_cfunction(ctx, L"of", neo_js_array_of);
  neo_js_context_def_field(ctx, ctx->std.array_constructor,
                           neo_js_context_create_string(ctx, L"of"), of, true,
                           false, true);

  neo_js_variable_t species = neo_js_context_create_cfunction(
      ctx, L"[Symbol.species]", neo_js_array_species);
  neo_js_context_def_accessor(
      ctx, ctx->std.array_constructor,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"species"),
                               NULL),
      species, NULL, true, false);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.array_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t at =
      neo_js_context_create_cfunction(ctx, L"at", neo_js_array_at);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"at"), at, true,
                           false, true);

  neo_js_variable_t concat =
      neo_js_context_create_cfunction(ctx, L"concat", neo_js_array_concat);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"concat"), concat,
                           true, false, true);

  neo_js_variable_t copy_within = neo_js_context_create_cfunction(
      ctx, L"copyWithin", neo_js_array_copy_within);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"copyWithin"),
                           copy_within, true, false, true);

  neo_js_variable_t entries =
      neo_js_context_create_cfunction(ctx, L"entries", neo_js_array_entries);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"entries"),
                           entries, true, false, true);

  neo_js_variable_t every =
      neo_js_context_create_cfunction(ctx, L"every", neo_js_array_every);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"every"), every,
                           true, false, true);

  neo_js_variable_t fill =
      neo_js_context_create_cfunction(ctx, L"fill", neo_js_array_fill);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"fill"), fill,
                           true, false, true);

  neo_js_variable_t filter =
      neo_js_context_create_cfunction(ctx, L"filter", neo_js_array_filter);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"filter"), filter,
                           true, false, true);

  neo_js_variable_t find =
      neo_js_context_create_cfunction(ctx, L"find", neo_js_array_find);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"find"), find,
                           true, false, true);

  neo_js_variable_t find_index = neo_js_context_create_cfunction(
      ctx, L"findIndex", neo_js_array_find_index);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"findIndex"),
                           find_index, true, false, true);

  neo_js_variable_t find_last =
      neo_js_context_create_cfunction(ctx, L"findLast", neo_js_array_find_last);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"findLast"),
                           find_last, true, false, true);

  neo_js_variable_t find_last_index = neo_js_context_create_cfunction(
      ctx, L"findLastIndex", neo_js_array_find_last_index);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"findLastIndex"),
                           find_last_index, true, false, true);

  neo_js_variable_t flat =
      neo_js_context_create_cfunction(ctx, L"flat", neo_js_array_flat);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"flat"), flat,
                           true, false, true);

  neo_js_variable_t flat_map =
      neo_js_context_create_cfunction(ctx, L"flatMap", neo_js_array_flat_map);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"flat_map"),
                           flat_map, true, false, true);

  neo_js_variable_t for_each =
      neo_js_context_create_cfunction(ctx, L"forEach", neo_js_array_for_each);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"forEach"),
                           for_each, true, false, true);

  neo_js_variable_t includes =
      neo_js_context_create_cfunction(ctx, L"includes", neo_js_array_includes);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"includes"),
                           includes, true, false, true);

  neo_js_variable_t index_of =
      neo_js_context_create_cfunction(ctx, L"indexOf", neo_js_array_index_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"indexOf"),
                           index_of, true, false, true);

  neo_js_variable_t join =
      neo_js_context_create_cfunction(ctx, L"join", neo_js_array_join);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"join"), join,
                           true, false, true);

  neo_js_variable_t keys =
      neo_js_context_create_cfunction(ctx, L"keys", neo_js_array_keys);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"keys"), keys,
                           true, false, true);

  neo_js_variable_t last_index_of = neo_js_context_create_cfunction(
      ctx, L"lastIndexOf", neo_js_array_last_index_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"lastIndexOf"),
                           last_index_of, true, false, true);

  neo_js_variable_t map =
      neo_js_context_create_cfunction(ctx, L"map", neo_js_array_map);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"map"), map, true,
                           false, true);

  neo_js_variable_t pop =
      neo_js_context_create_cfunction(ctx, L"pop", neo_js_array_pop);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"pop"), pop, true,
                           false, true);

  neo_js_variable_t push =
      neo_js_context_create_cfunction(ctx, L"push", neo_js_array_push);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"push"), push,
                           true, false, true);

  neo_js_variable_t reduce =
      neo_js_context_create_cfunction(ctx, L"reduce", neo_js_array_reduce);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"reduce"), reduce,
                           true, false, true);

  neo_js_variable_t reduce_right = neo_js_context_create_cfunction(
      ctx, L"reduceRight", neo_js_array_reduce_right);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"reduceRight"),
                           reduce_right, true, false, true);

  neo_js_variable_t reverse =
      neo_js_context_create_cfunction(ctx, L"reverse", neo_js_array_reverse);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"reverse"),
                           reverse, true, false, true);

  neo_js_variable_t shift =
      neo_js_context_create_cfunction(ctx, L"shift", neo_js_array_shift);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"shift"), shift,
                           true, false, true);

  neo_js_variable_t slice =
      neo_js_context_create_cfunction(ctx, L"slice", neo_js_array_slice);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"slice"), slice,
                           true, false, true);

  neo_js_variable_t some =
      neo_js_context_create_cfunction(ctx, L"some", neo_js_array_some);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"some"), some,
                           true, false, true);

  neo_js_variable_t sort =
      neo_js_context_create_cfunction(ctx, L"sort", neo_js_array_sort);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"sort"), sort,
                           true, false, true);

  neo_js_variable_t splice =
      neo_js_context_create_cfunction(ctx, L"splice", neo_js_array_splice);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"splice"), splice,
                           true, false, true);

  neo_js_variable_t to_local_string = neo_js_context_create_cfunction(
      ctx, L"toLocalString", neo_js_array_to_local_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toLocalString"),
                           to_local_string, true, false, true);

  neo_js_variable_t to_reversed = neo_js_context_create_cfunction(
      ctx, L"toReversed", neo_js_array_to_reversed);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toReversed"),
                           to_reversed, true, false, true);

  neo_js_variable_t to_sorted =
      neo_js_context_create_cfunction(ctx, L"toSorted", neo_js_array_to_sorted);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toSorted"),
                           to_sorted, true, false, true);

  neo_js_variable_t to_spliced = neo_js_context_create_cfunction(
      ctx, L"toSpliced", neo_js_array_to_spliced);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toSpliced"),
                           to_spliced, true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString", neo_js_array_to_string),
      true, false, true);

  neo_js_variable_t unshift =
      neo_js_context_create_cfunction(ctx, L"unshift", neo_js_array_unshift);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"unshift"),
                           unshift, true, false, true);

  neo_js_variable_t values =
      neo_js_context_create_cfunction(ctx, L"values", neo_js_array_values);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"values"), values,
                           true, false, true);

  neo_js_variable_t with =
      neo_js_context_create_cfunction(ctx, L"with", neo_js_array_with);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"with"), with,
                           true, false, true);

  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"iterator"), NULL);
  neo_js_context_def_field(ctx, prototype, iterator, values, true, false, true);
}

static void neo_js_context_init_std_generator_function(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.generator_function_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

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
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(
          ctx, L"toString", neo_js_async_generator_function_to_string),
      true, false, true);
}
static void neo_js_context_init_std_async_function(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.async_function_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString",
                                      neo_js_async_function_to_string),
      true, false, true);
}
static void neo_js_context_init_std_generator(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.generator_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t to_string_tag = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"toStringTag"), NULL);

  neo_js_context_def_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, L"Generator"),
                           true, false, true);

  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"iterator"), NULL);

  neo_js_context_def_field(
      ctx, prototype, iterator,
      neo_js_context_create_cfunction(ctx, L"[Symbol.iterator]",
                                      neo_js_generator_iterator),
      true, false, true);

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
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.async_generator_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t to_string_tag = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"toStringTag"), NULL);

  neo_js_context_def_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, L"AsyncGenerator"),
                           true, false, true);

  neo_js_variable_t async_iterator = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"asyncIterator"), NULL);

  neo_js_context_def_field(
      ctx, prototype, async_iterator,
      neo_js_context_create_cfunction(ctx, L"[Symbol.asyncIterator]",
                                      neo_js_async_generator_iterator),
      true, false, true);

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

static void neo_js_context_init_std_map(neo_js_context_t ctx) {
  neo_js_variable_t group_by =
      neo_js_context_create_cfunction(ctx, L"groupBy", neo_js_map_group_by);
  neo_js_context_def_field(ctx, ctx->std.map_constructor,
                           neo_js_context_create_string(ctx, L"groupBy"),
                           group_by, true, false, true);

  neo_js_variable_t species = neo_js_context_create_cfunction(
      ctx, L"[Symbol.species]", neo_js_map_species);
  neo_js_context_def_accessor(
      ctx, ctx->std.map_constructor,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"species"),
                               NULL),
      species, NULL, true, false);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.map_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);
  neo_js_variable_t clear =
      neo_js_context_create_cfunction(ctx, L"clear", neo_js_map_clear);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"clear"), clear,
                           true, false, true);
  neo_js_variable_t delete =
      neo_js_context_create_cfunction(ctx, L"delete", neo_js_map_delete);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"delete"), delete,
                           true, false, true);
  neo_js_variable_t entries =
      neo_js_context_create_cfunction(ctx, L"entries", neo_js_map_entries);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"entries"),
                           entries, true, false, true);
  neo_js_variable_t for_each =
      neo_js_context_create_cfunction(ctx, L"forEach", neo_js_map_for_each);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"forEach"),
                           for_each, true, false, true);
  neo_js_variable_t get =
      neo_js_context_create_cfunction(ctx, L"get", neo_js_map_get);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"get"), get, true,
                           false, true);
  neo_js_variable_t has =
      neo_js_context_create_cfunction(ctx, L"has", neo_js_map_has);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"has"), has, true,
                           false, true);
  neo_js_variable_t keys =
      neo_js_context_create_cfunction(ctx, L"keys", neo_js_map_keys);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"keys"), keys,
                           true, false, true);
  neo_js_variable_t set =
      neo_js_context_create_cfunction(ctx, L"set", neo_js_map_set);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"set"), set, true,
                           false, true);
  neo_js_variable_t values =
      neo_js_context_create_cfunction(ctx, L"values", neo_js_map_values);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"values"), values,
                           true, false, true);
  neo_js_context_def_field(
      ctx, prototype,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"iterator"),
                               NULL),
      entries, true, false, true);
}

static void neo_js_context_init_std_math(neo_js_context_t ctx) {
  neo_js_variable_t math =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));

  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Math"), math,
                           NULL);

  neo_js_context_set_field(ctx, math, neo_js_context_create_string(ctx, L"E"),
                           neo_js_context_create_number(ctx, M_E), NULL);

  neo_js_context_set_field(ctx, math, neo_js_context_create_string(ctx, L"LN2"),
                           neo_js_context_create_number(ctx, M_LN2), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, L"LN10"),
                           neo_js_context_create_number(ctx, M_LN10), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, L"LOG2E"),
                           neo_js_context_create_number(ctx, M_LOG2E), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, L"LOG10E"),
                           neo_js_context_create_number(ctx, M_LOG10E), NULL);

  neo_js_context_set_field(ctx, math, neo_js_context_create_string(ctx, L"PI"),
                           neo_js_context_create_number(ctx, M_PI), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, L"SQRT1_2"),
                           neo_js_context_create_number(ctx, M_SQRT1_2), NULL);

  neo_js_context_set_field(ctx, math,
                           neo_js_context_create_string(ctx, L"SQRT2"),
                           neo_js_context_create_number(ctx, M_SQRT2), NULL);

  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"abs"),
      neo_js_context_create_cfunction(ctx, L"abs", neo_js_math_abs), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"acos"),
      neo_js_context_create_cfunction(ctx, L"acos", neo_js_math_acos), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"acosh"),
      neo_js_context_create_cfunction(ctx, L"acosh", neo_js_math_acosh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"asin"),
      neo_js_context_create_cfunction(ctx, L"asin", neo_js_math_asin), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"asinh"),
      neo_js_context_create_cfunction(ctx, L"asinh", neo_js_math_asinh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"atan"),
      neo_js_context_create_cfunction(ctx, L"atan", neo_js_math_atan), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"atan2"),
      neo_js_context_create_cfunction(ctx, L"atan2", neo_js_math_atan2), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"atanh"),
      neo_js_context_create_cfunction(ctx, L"atanh", neo_js_math_atanh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"cbrt"),
      neo_js_context_create_cfunction(ctx, L"cbrt", neo_js_math_cbrt), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"ceil"),
      neo_js_context_create_cfunction(ctx, L"ceil", neo_js_math_ceil), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"clz32"),
      neo_js_context_create_cfunction(ctx, L"clz32", neo_js_math_clz32), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"cos"),
      neo_js_context_create_cfunction(ctx, L"cos", neo_js_math_cos), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"cosh"),
      neo_js_context_create_cfunction(ctx, L"cosh", neo_js_math_cosh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"exp"),
      neo_js_context_create_cfunction(ctx, L"exp", neo_js_math_exp), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"expm1"),
      neo_js_context_create_cfunction(ctx, L"expm1", neo_js_math_expm1), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"floor"),
      neo_js_context_create_cfunction(ctx, L"floor", neo_js_math_floor), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"fround"),
      neo_js_context_create_cfunction(ctx, L"fround", neo_js_math_fround), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"hypot"),
      neo_js_context_create_cfunction(ctx, L"hypot", neo_js_math_hypot), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"imul"),
      neo_js_context_create_cfunction(ctx, L"imul", neo_js_math_imul), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"log"),
      neo_js_context_create_cfunction(ctx, L"log", neo_js_math_log), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"log1p"),
      neo_js_context_create_cfunction(ctx, L"log1p", neo_js_math_log1p), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"log2"),
      neo_js_context_create_cfunction(ctx, L"log2", neo_js_math_log2), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"log10"),
      neo_js_context_create_cfunction(ctx, L"log10", neo_js_math_log10), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"max"),
      neo_js_context_create_cfunction(ctx, L"max", neo_js_math_max), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"min"),
      neo_js_context_create_cfunction(ctx, L"min", neo_js_math_min), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"pow"),
      neo_js_context_create_cfunction(ctx, L"pow", neo_js_math_pow), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"random"),
      neo_js_context_create_cfunction(ctx, L"random", neo_js_math_random), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"round"),
      neo_js_context_create_cfunction(ctx, L"round", neo_js_math_round), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"sign"),
      neo_js_context_create_cfunction(ctx, L"sign", neo_js_math_sign), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"sin"),
      neo_js_context_create_cfunction(ctx, L"sin", neo_js_math_sin), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"sinh"),
      neo_js_context_create_cfunction(ctx, L"sinh", neo_js_math_sinh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"sqrt"),
      neo_js_context_create_cfunction(ctx, L"sqrt", neo_js_math_sqrt), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"tan"),
      neo_js_context_create_cfunction(ctx, L"tan", neo_js_math_tan), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"tanh"),
      neo_js_context_create_cfunction(ctx, L"tanh", neo_js_math_tanh), true,
      false, true);
  neo_js_context_def_field(
      ctx, math, neo_js_context_create_string(ctx, L"trunc"),
      neo_js_context_create_cfunction(ctx, L"trunc", neo_js_math_trunc), true,
      false, true);
}

static void neo_js_context_init_std_number(neo_js_context_t ctx) {
  neo_js_variable_t is_finite = neo_js_context_create_cfunction(
      ctx, L"isFinite", neo_js_number_is_finite);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"isFinite"),
                           is_finite, true, false, true);
  neo_js_variable_t is_integer = neo_js_context_create_cfunction(
      ctx, L"isInteger", neo_js_number_is_integer);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"isInteger"),
                           is_integer, true, false, true);
  neo_js_variable_t is_nan =
      neo_js_context_create_cfunction(ctx, L"isNaN", neo_js_number_is_nan);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"isNaN"), is_nan,
                           true, false, true);
  neo_js_variable_t is_safe_integer = neo_js_context_create_cfunction(
      ctx, L"isSafeInteger", neo_js_number_is_safe_integer);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"isSafeInteger"),
                           is_safe_integer, true, false, true);
  neo_js_variable_t epsilon =
      neo_js_context_create_number(ctx, 2.220446049250313e-16);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"EPSILON"),
                           epsilon, false, false, false);
  neo_js_variable_t max_safe_integer =
      neo_js_context_create_number(ctx, NEO_MAX_INTEGER);
  neo_js_context_def_field(
      ctx, ctx->std.number_constructor,
      neo_js_context_create_string(ctx, L"MAX_SAFE_INTEGER"), max_safe_integer,
      false, false, false);
  neo_js_variable_t max_value = neo_js_context_create_number(ctx, 2e52 - 1);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"MAX_VALUE"),
                           max_value, false, false, false);
  neo_js_variable_t min_safe_integer =
      neo_js_context_create_number(ctx, -9007199254740991);
  neo_js_context_def_field(
      ctx, ctx->std.number_constructor,
      neo_js_context_create_string(ctx, L"MIN_SAFE_INTEGER"), min_safe_integer,
      false, false, false);

  neo_js_variable_t min_value = neo_js_context_create_number(ctx, 5E-324);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"MIN_VALUE"),
                           min_value, false, false, false);
  neo_js_variable_t nan = neo_js_context_create_number(ctx, NAN);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"NaN"), nan,
                           false, false, false);
  neo_js_variable_t negative_infinity =
      neo_js_context_create_number(ctx, -INFINITY);
  neo_js_context_def_field(
      ctx, ctx->std.number_constructor,
      neo_js_context_create_string(ctx, L"NEGATIVE_INFINITY"),
      negative_infinity, false, false, false);

  neo_js_variable_t positive_infinity =
      neo_js_context_create_number(ctx, INFINITY);
  neo_js_context_def_field(
      ctx, ctx->std.number_constructor,
      neo_js_context_create_string(ctx, L"POSITIVE_INFINITY"),
      positive_infinity, false, false, false);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.number_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);
  neo_js_variable_t to_exponential = neo_js_context_create_cfunction(
      ctx, L"toExponential", neo_js_number_to_exponential);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toExponential"),
                           to_exponential, true, false, true);
  neo_js_variable_t to_fixed =
      neo_js_context_create_cfunction(ctx, L"toFixed", neo_js_number_to_fixed);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toFixed"),
                           to_fixed, true, false, true);
  neo_js_variable_t to_local_string = neo_js_context_create_cfunction(
      ctx, L"toLocalString", neo_js_number_to_local_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toLocalString"),
                           to_local_string, true, false, true);
  neo_js_variable_t to_precision = neo_js_context_create_cfunction(
      ctx, L"toPrecision", neo_js_number_to_precision);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toPrecision"),
                           to_precision, true, false, true);
  neo_js_variable_t to_string = neo_js_context_create_cfunction(
      ctx, L"toString", neo_js_number_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           to_string, true, false, true);
  neo_js_variable_t value_of =
      neo_js_context_create_cfunction(ctx, L"valueOf", neo_js_number_value_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"valueOf"),
                           value_of, true, false, true);
}

static void neo_js_context_init_std_array_iterator(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.array_iterator_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t to_string_tag = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"toStringTag"), NULL);

  neo_js_context_def_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, L"ArrayIterator"),
                           true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"next"),
      neo_js_context_create_cfunction(ctx, L"next", neo_js_array_iterator_next),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"iterator"),
                               NULL),
      neo_js_context_create_cfunction(ctx, L"iterator",
                                      neo_js_array_iterator_iterator),
      true, false, true);
}

static void neo_js_context_init_std_aggregate_error(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.aggregate_error_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString", neo_js_error_to_string),
      true, false, true);
}

static void neo_js_context_init_std_error(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.error_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString", neo_js_error_to_string),
      true, false, true);
}

static void neo_js_context_init_lib_json(neo_js_context_t ctx) {
  neo_js_variable_t json =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));

  neo_js_context_def_field(
      ctx, json, neo_js_context_create_string(ctx, L"parse"),
      neo_js_context_create_cfunction(ctx, L"parse", neo_js_json_parse), true,
      false, true);

  neo_js_context_def_field(
      ctx, json, neo_js_context_create_string(ctx, L"stringify"),
      neo_js_context_create_cfunction(ctx, L"stringify", neo_js_json_stringify),
      true, false, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"JSON"), json,
                           true, false, true);
}
static void neo_js_context_init_std_promise(neo_js_context_t ctx) {
  neo_js_context_def_field(
      ctx, ctx->std.promise_constructor,
      neo_js_context_create_string(ctx, L"all"),
      neo_js_context_create_cfunction(ctx, L"all", neo_js_promise_all), true,
      false, true);

  neo_js_context_def_field(ctx, ctx->std.promise_constructor,
                           neo_js_context_create_string(ctx, L"allSettled"),
                           neo_js_context_create_cfunction(
                               ctx, L"allSettled", neo_js_promise_all_settled),
                           true, false, true);

  neo_js_context_def_field(
      ctx, ctx->std.promise_constructor,
      neo_js_context_create_string(ctx, L"any"),
      neo_js_context_create_cfunction(ctx, L"any", neo_js_promise_any), true,
      false, true);

  neo_js_context_def_field(
      ctx, ctx->std.promise_constructor,
      neo_js_context_create_string(ctx, L"race"),
      neo_js_context_create_cfunction(ctx, L"race", neo_js_promise_race), true,
      false, true);

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

  neo_js_context_def_field(
      ctx, ctx->std.promise_constructor,
      neo_js_context_create_string(ctx, L"try"),
      neo_js_context_create_cfunction(ctx, L"try", neo_js_promise_try), true,
      false, true);

  neo_js_context_def_field(
      ctx, ctx->std.promise_constructor,
      neo_js_context_create_string(ctx, L"withResolvers"),
      neo_js_context_create_cfunction(ctx, L"withResolvers",
                                      neo_js_promise_with_resolvers),
      true, false, true);

  neo_js_context_def_accessor(
      ctx, ctx->std.promise_constructor,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"species"),
                               NULL),
      neo_js_context_create_cfunction(ctx, L"[Symbol.species]",
                                      neo_js_promise_species),
      NULL, true, true);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.promise_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t to_string_tag = neo_js_context_get_field(
      ctx, ctx->std.symbol_constructor,
      neo_js_context_create_string(ctx, L"toStringTag"), NULL);

  neo_js_context_def_field(ctx, prototype, to_string_tag,
                           neo_js_context_create_string(ctx, L"Promise"), true,
                           false, true);

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

static void neo_js_context_init_std_reflect(neo_js_context_t ctx) {
  neo_js_variable_t reflect =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));
  neo_js_context_set_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Reflect"),
                           reflect, NULL);
  neo_js_context_set_field(
      ctx, reflect,
      neo_js_context_get_field(
          ctx, ctx->std.symbol_constructor,
          neo_js_context_create_string(ctx, L"toStringTag"), NULL),
      neo_js_context_create_string(ctx, L"Reflect"), NULL);
  neo_js_variable_t apply =
      neo_js_context_create_cfunction(ctx, L"apply", neo_js_reflect_apply);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"apply"), apply,
                           true, false, true);
  neo_js_variable_t construct = neo_js_context_create_cfunction(
      ctx, L"construct", neo_js_reflect_construct);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"construct"),
                           construct, true, false, true);
  neo_js_variable_t define_property = neo_js_context_create_cfunction(
      ctx, L"defineProperty", neo_js_reflect_define_property);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"defineProperty"),
                           define_property, true, false, true);
  neo_js_variable_t delete_property = neo_js_context_create_cfunction(
      ctx, L"deleteProperty", neo_js_reflect_delete_property);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"deleteProperty"),
                           delete_property, true, false, true);
  neo_js_variable_t get =
      neo_js_context_create_cfunction(ctx, L"get", neo_js_reflect_get);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"get"), get, true,
                           false, true);
  neo_js_variable_t get_own_property_descriptor =
      neo_js_context_create_cfunction(
          ctx, L"getOwnPropertyDescriptor",
          neo_js_reflect_get_own_property_descriptor);
  neo_js_context_def_field(
      ctx, reflect,
      neo_js_context_create_string(ctx, L"getOwnPropertyDescriptor"),
      get_own_property_descriptor, true, false, true);
  neo_js_variable_t get_prototype_of = neo_js_context_create_cfunction(
      ctx, L"getPrototypeOf", neo_js_reflect_get_prototype_of);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"getPrototypeOf"),
                           get_prototype_of, true, false, true);
  neo_js_variable_t has =
      neo_js_context_create_cfunction(ctx, L"has", neo_js_reflect_has);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"has"), has, true,
                           false, true);
  neo_js_variable_t is_extensible = neo_js_context_create_cfunction(
      ctx, L"isExtensible", neo_js_reflect_is_extensible);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"isExtensible"),
                           is_extensible, true, false, true);
  neo_js_variable_t own_keys =
      neo_js_context_create_cfunction(ctx, L"ownKeys", neo_js_reflect_own_keys);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"ownKeys"),
                           own_keys, true, false, true);
  neo_js_variable_t prevent_extensions = neo_js_context_create_cfunction(
      ctx, L"preventExtensions", neo_js_reflect_prevent_extensions);
  neo_js_context_def_field(
      ctx, reflect, neo_js_context_create_string(ctx, L"preventExtensions"),
      prevent_extensions, true, false, true);
  neo_js_variable_t set =
      neo_js_context_create_cfunction(ctx, L"set", neo_js_reflect_set);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"set"), set, true,
                           false, true);
  neo_js_variable_t set_prototype_of = neo_js_context_create_cfunction(
      ctx, L"setPrototypeOf", neo_js_reflect_set_prototype_of);
  neo_js_context_def_field(ctx, reflect,
                           neo_js_context_create_string(ctx, L"setPrototypeOf"),
                           set_prototype_of, true, false, true);
}

static void neo_js_context_init_std_regexp(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.regexp_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           neo_js_context_create_cfunction(
                               ctx, L"toString", neo_js_regexp_to_string),
                           true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"exec"),
      neo_js_context_create_cfunction(ctx, L"exec", neo_js_regexp_exec), true,
      false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"test"),
      neo_js_context_create_cfunction(ctx, L"test", neo_js_regexp_test), true,
      false, true);
}

static void neo_js_context_init_std_console(neo_js_context_t ctx) {
  neo_js_variable_t console_constructor = neo_js_context_create_cfunction(
      ctx, L"Console", neo_js_console_constructor);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, console_constructor, neo_js_context_create_string(ctx, L"prototype"),
      NULL);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"log"),
      neo_js_context_create_cfunction(ctx, L"log", neo_js_console_log), true,
      false, true);

  neo_js_variable_t console =
      neo_js_context_construct(ctx, console_constructor, 0, NULL);
  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"console"),
                           console, true, true, true);
}

static void neo_js_context_init_std_bigint(neo_js_context_t ctx) {
  neo_js_context_def_field(
      ctx, ctx->std.bigint_constructor,
      neo_js_context_create_string(ctx, L"asIntN"),
      neo_js_context_create_cfunction(ctx, L"asIntN", neo_js_bigint_as_int_n),
      true, false, true);

  neo_js_context_def_field(
      ctx, ctx->std.bigint_constructor,
      neo_js_context_create_string(ctx, L"asUintN"),
      neo_js_context_create_cfunction(ctx, L"asUintN", neo_js_bigint_as_uint_n),
      true, false, true);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.bigint_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t value_of =
      neo_js_context_create_cfunction(ctx, L"valueOf", neo_js_bigint_value_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"valueOf"),
                           value_of, true, false, true);

  neo_js_variable_t to_string = neo_js_context_create_cfunction(
      ctx, L"toString", neo_js_bigint_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           to_string, true, false, true);

  neo_js_variable_t to_local_string = neo_js_context_create_cfunction(
      ctx, L"toLocalString", neo_js_bigint_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toLocalString"),
                           to_local_string, true, false, true);
}

static void neo_js_context_init_std_boolean(neo_js_context_t ctx) {

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.boolean_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t value_of =
      neo_js_context_create_cfunction(ctx, L"valueOf", neo_js_boolean_value_of);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"valueOf"),
                           value_of, true, false, true);

  neo_js_variable_t to_string = neo_js_context_create_cfunction(
      ctx, L"toString", neo_js_boolean_to_string);
  neo_js_context_def_field(ctx, prototype,
                           neo_js_context_create_string(ctx, L"toString"),
                           to_string, true, false, true);
}

static void neo_js_context_init_lib(neo_js_context_t ctx) {
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, L"setTimeout"),
      neo_js_context_create_cfunction(ctx, L"setTimeout", neo_js_set_timeout),
      true, true, true);
  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"clearTimeout"),
                           neo_js_context_create_cfunction(
                               ctx, L"clearTimeout", neo_js_clear_timeout),
                           true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, L"setInterval"),
      neo_js_context_create_cfunction(ctx, L"setInterval", neo_js_set_interval),
      true, true, true);
  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"clearInterval"),
                           neo_js_context_create_cfunction(
                               ctx, L"clearInterval", neo_js_clear_interval),
                           true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, L"encodeURI"),
      neo_js_context_create_cfunction(ctx, L"encodeURI", neo_js_encode_uri),
      true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global,
      neo_js_context_create_string(ctx, L"encodeURIComponent"),
      neo_js_context_create_cfunction(ctx, L"encodeURIComponent",
                                      neo_js_encode_uri_component),
      true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, L"decodeURI"),
      neo_js_context_create_cfunction(ctx, L"decodeURI", neo_js_decode_uri),
      true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global,
      neo_js_context_create_string(ctx, L"decodeURIComponent"),
      neo_js_context_create_cfunction(ctx, L"decodeURIComponent",
                                      neo_js_decode_uri_component),
      true, true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, L"eval"),
      neo_js_context_create_cfunction(ctx, L"eval", neo_js_eval), true, true,
      true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, L"isFinite"),
      neo_js_context_create_cfunction(ctx, L"isFinite", neo_js_is_finite), true,
      true, true);
  neo_js_context_def_field(
      ctx, ctx->std.global, neo_js_context_create_string(ctx, L"isNaN"),
      neo_js_context_create_cfunction(ctx, L"isNaN", neo_js_is_nan), true, true,
      true);

  neo_js_variable_t parse_float =
      neo_js_context_create_cfunction(ctx, L"parseFloat", neo_js_parse_float);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"parseFloat"),
                           parse_float, true, true, true);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"parseFloat"),
                           parse_float, true, false, true);

  neo_js_variable_t parse_int =
      neo_js_context_create_cfunction(ctx, L"parseInt", neo_js_parse_int);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"parseInt"),
                           parse_int, true, true, true);
  neo_js_context_def_field(ctx, ctx->std.number_constructor,
                           neo_js_context_create_string(ctx, L"parseInt"),
                           parse_int, true, false, true);

  neo_js_context_init_lib_json(ctx);
}
static void neo_js_context_init_std_date(neo_js_context_t ctx) {
  neo_js_context_def_field(
      ctx, ctx->std.date_constructor, neo_js_context_create_string(ctx, L"now"),
      neo_js_context_create_cfunction(ctx, L"now", neo_js_date_now), true,
      false, true);

  neo_js_variable_t parse =
      neo_js_context_create_cfunction(ctx, L"parse", neo_js_date_parse);
  neo_js_context_def_field(ctx, ctx->std.date_constructor,
                           neo_js_context_create_string(ctx, L"parse"), parse,
                           true, false, true);

  neo_js_variable_t utc =
      neo_js_context_create_cfunction(ctx, L"UTC", neo_js_date_utc);
  neo_js_context_def_field(ctx, ctx->std.date_constructor,
                           neo_js_context_create_string(ctx, L"UTC"), utc, true,
                           false, true);

  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, ctx->std.date_constructor,
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
          ctx, ctx->std.symbol_constructor,
          neo_js_context_create_string(ctx, L"toPrimitive"), NULL),
      to_primitive, true, false, true);
}

static void neo_js_context_init_std(neo_js_context_t ctx) {
  ctx->std.object_constructor = neo_js_context_create_cfunction(
      ctx, L"Object", &neo_js_object_constructor);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t prototype =
      neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));

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
                           neo_js_context_create_object(ctx, NULL), true, false,
                           true);

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

  ctx->std.aggregate_error_constructor = neo_js_context_create_cfunction(
      ctx, L"AggregateError", neo_js_aggregate_error_constructor);
  neo_js_context_extends(ctx, ctx->std.aggregate_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.range_error_constructor = neo_js_context_create_cfunction(
      ctx, L"RangeError", neo_js_range_error_constructor);
  neo_js_context_extends(ctx, ctx->std.range_error_constructor,
                         ctx->std.error_constructor);

  ctx->std.uri_error_constructor = neo_js_context_create_cfunction(
      ctx, L"URIError", neo_js_uri_error_constructor);
  neo_js_context_extends(ctx, ctx->std.uri_error_constructor,
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

  ctx->std.internal_error_constructor = neo_js_context_create_cfunction(
      ctx, L"InternalError", neo_js_internal_error_constructor);
  neo_js_context_extends(ctx, ctx->std.internal_error_constructor,
                         ctx->std.error_constructor);

  neo_js_variable_t array_prototype = neo_js_context_create_array(ctx);
  ctx->std.array_constructor =
      neo_js_context_create_cfunction(ctx, L"Array", neo_js_array_constructor);

  neo_js_context_def_field(ctx, ctx->std.array_constructor,
                           neo_js_context_create_string(ctx, L"prototype"),
                           array_prototype, true, false, true);

  neo_js_context_def_field(ctx, array_prototype,
                           neo_js_context_create_string(ctx, L"constructor"),
                           ctx->std.array_constructor, true, false, true);

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

  ctx->std.async_generator_constructor = neo_js_context_create_cfunction(
      ctx, L"AsyncGenerator", neo_js_async_generator_constructor);

  ctx->std.bigint_constructor = neo_js_context_create_cfunction(
      ctx, L"BigInt", neo_js_bigint_constructor);

  ctx->std.boolean_constructor = neo_js_context_create_cfunction(
      ctx, L"Boolean", neo_js_boolean_constructor);

  ctx->std.date_constructor =
      neo_js_context_create_cfunction(ctx, L"Date", neo_js_date_constructor);

  ctx->std.generator_constructor = neo_js_context_create_cfunction(
      ctx, L"Generator", neo_js_generator_constructor);

  ctx->std.map_constructor =
      neo_js_context_create_cfunction(ctx, L"Map", neo_js_map_constructor);

  ctx->std.promise_constructor = neo_js_context_create_cfunction(
      ctx, L"Promise", neo_js_promise_constructor);

  ctx->std.regexp_constructor = neo_js_context_create_cfunction(
      ctx, L"RegExp", neo_js_regexp_constructor);

  ctx->std.number_constructor = neo_js_context_create_cfunction(
      ctx, L"Number", neo_js_number_constructor);

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

  neo_js_context_extends(ctx, ctx->std.generator_function_constructor,
                         ctx->std.function_constructor);

  neo_js_context_extends(ctx, ctx->std.async_function_constructor,
                         ctx->std.function_constructor);

  neo_js_context_extends(ctx, ctx->std.async_generator_function_constructor,
                         ctx->std.function_constructor);

  neo_js_context_init_std_console(ctx);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"global"),
                           ctx->std.global, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Error"),
                           ctx->std.error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"AggregateError"),
                           ctx->std.aggregate_error_constructor, true, true,
                           true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"RangeError"),
                           ctx->std.range_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"URIError"),
                           ctx->std.uri_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"TypeError"),
                           ctx->std.type_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"SyntaxError"),
                           ctx->std.syntax_error_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"ReferenceError"),
                           ctx->std.reference_error_constructor, true, true,
                           true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Function"),
                           ctx->std.function_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Object"),
                           ctx->std.object_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Symbol"),
                           ctx->std.symbol_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Array"),
                           ctx->std.array_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Promise"),
                           ctx->std.promise_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"RegExp"),
                           ctx->std.regexp_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"BigInt"),
                           ctx->std.bigint_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Boolean"),
                           ctx->std.boolean_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Date"),
                           ctx->std.date_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Map"),
                           ctx->std.map_constructor, true, true, true);

  neo_js_context_def_field(ctx, ctx->std.global,
                           neo_js_context_create_string(ctx, L"Number"),
                           ctx->std.number_constructor, true, true, true);

  neo_js_context_pop_scope(ctx);
}

static neo_js_variable_t neo_js_context_default_assert(neo_js_context_t ctx,
                                                       const wchar_t *type,
                                                       const wchar_t *value,
                                                       const wchar_t *file) {
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
  module_initialize.compare = (neo_compare_fn_t)wcscmp;
  module_initialize.auto_free_key = true;
  module_initialize.auto_free_value = false;
  ctx->modules = neo_create_hash_map(allocator, &module_initialize);
  neo_hash_map_initialize_t feature_initialize = {0};
  feature_initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  feature_initialize.compare = (neo_compare_fn_t)wcscmp;
  feature_initialize.auto_free_key = true;
  feature_initialize.auto_free_value = true;
  ctx->features = neo_create_hash_map(allocator, &feature_initialize);
  neo_js_stackframe_t frame = neo_create_js_stackframe(allocator);
  frame->function = neo_create_wstring(allocator, L"start");
  neo_list_push(ctx->stacktrace, frame);
  neo_js_context_push_stackframe(ctx, NULL, L"_.eval", 0, 0);
  neo_js_context_init_std(ctx);
  neo_js_context_init_lib(ctx);
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
                              neo_destructor_fn_t dispose, const char *filename,
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
    neo_js_handle_remove_parent(task->function,
                                neo_js_scope_get_root_handle(ctx->task));
  }
  if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
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
      target->filename = neo_create_wstring(allocator, frame->filename);
    }
    if (frame->function) {
      target->function = neo_create_wstring(allocator, frame->function);
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

neo_js_std_t neo_js_context_get_std(neo_js_context_t ctx) { return ctx->std; }

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t ctx,
                                                 neo_js_handle_t handle,
                                                 const wchar_t *name) {
  return neo_js_scope_create_variable(ctx->scope, handle, NULL);
}

neo_js_variable_t neo_js_context_def_variable(neo_js_context_t ctx,
                                              neo_js_variable_t variable,
                                              const wchar_t *name) {
  neo_js_variable_t current = neo_js_scope_get_variable(ctx->scope, name);
  if (current != NULL) {
    wchar_t msg[1024];
    swprintf(msg, 1024, L"cannot redefine variable: '%ls'", name);
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX, msg);
  }
  neo_js_handle_t handle = neo_js_variable_get_handle(variable);
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
                                                const wchar_t *name) {
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
          ctx, NEO_JS_ERROR_TYPE, L"assignment to constant variable.");
    }
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
      return variable;
    }
    scope = neo_js_scope_get_parent(scope);
  }
  neo_js_variable_t field = neo_js_context_create_string(ctx, name);
  if (neo_js_context_has_field(ctx, ctx->std.global, field)) {
    return neo_js_context_get_field(ctx, ctx->std.global, field, NULL);
  }
  wchar_t msg[1024];
  swprintf(msg, 1024, L"variable '%ls' is not defined", name);
  return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_REFERENCE, msg);
}

neo_js_variable_t neo_js_context_extends(neo_js_context_t ctx,
                                         neo_js_variable_t variable,
                                         neo_js_variable_t parent) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, variable, neo_js_context_create_string(ctx, L"prototype"), NULL);

  neo_js_variable_t parent_prototype = neo_js_context_get_field(
      ctx, parent, neo_js_context_create_string(ctx, L"prototype"), NULL);

  return neo_js_object_set_prototype(ctx, prototype, parent_prototype);
}

neo_js_variable_t neo_js_context_to_primitive(neo_js_context_t ctx,
                                              neo_js_variable_t variable,
                                              const wchar_t *hint) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_value_t value = neo_js_variable_get_value(variable);
  neo_js_variable_t result = value->type->to_primitive_fn(ctx, variable, hint);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
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
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field,
                                           neo_js_variable_t receiver) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->get_field_fn(ctx, object, field, NULL);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_set_field(neo_js_context_t ctx,
                                           neo_js_variable_t object,
                                           neo_js_variable_t field,
                                           neo_js_variable_t value,
                                           neo_js_variable_t receiver) {
  neo_js_scope_t current = ctx->scope;
  neo_js_context_push_scope(ctx);
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result =
      type->set_field_fn(ctx, object, field, value, NULL);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_private(neo_js_context_t ctx,
                                             neo_js_variable_t object,
                                             neo_js_variable_t field,
                                             neo_js_variable_t receiver) {
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_string_t key = neo_js_variable_to_string(field);
  if (!obj->privates) {
    size_t len = wcslen(key->string) + 64;
    wchar_t *message =
        neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
    swprintf(message, len,
             L"Private field '%ls' must be declared in an enclosing class.",
             key->string);
    neo_js_variable_t error =
        neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
    neo_allocator_free(allocator, message);
    return error;
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
        size_t len = wcslen(key->string) + 64;
        wchar_t *message =
            neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
        swprintf(message, len, L"Private field '%ls' hasn't getter.",
                 key->string);
        neo_js_variable_t error =
            neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
        neo_allocator_free(allocator, message);
        return error;
      }
    }
  } else {
    size_t len = wcslen(key->string) + 64;
    wchar_t *message =
        neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
    swprintf(message, len,
             L"Private field '%ls' must be declared in an enclosing class.",
             key->string);
    neo_js_variable_t error =
        neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
    neo_allocator_free(allocator, message);
    return error;
  }
}

neo_js_variable_t neo_js_context_set_private(neo_js_context_t ctx,
                                             neo_js_variable_t object,
                                             neo_js_variable_t field,
                                             neo_js_variable_t value,
                                             neo_js_variable_t receiver) {
  neo_js_object_t obj = neo_js_variable_to_object(object);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_string_t key = neo_js_variable_to_string(field);
  if (!obj->privates) {
    size_t len = wcslen(key->string) + 64;
    wchar_t *message =
        neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
    swprintf(message, len,
             L"Private field '%ls' must be declared in an enclosing class.",
             key->string);
    neo_js_variable_t error =
        neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
    neo_allocator_free(allocator, message);
    return error;
  }
  neo_js_object_private_t prop =
      neo_hash_map_get(obj->privates, key->string, NULL, NULL);
  if (!prop) {
    size_t len = wcslen(key->string) + 64;
    wchar_t *message =
        neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
    swprintf(message, len,
             L"Private field '%ls' must be declared in an enclosing class.",
             key->string);
    neo_js_variable_t error =
        neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
    neo_allocator_free(allocator, message);
    return error;
  } else {
    if (prop->value) {
      neo_js_handle_t root = neo_js_scope_get_root_handle(ctx->scope);
      neo_js_handle_add_parent(prop->value, root);
      prop->value = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(prop->value, neo_js_variable_get_handle(object));
    } else if (prop->set) {
      neo_js_variable_t setter =
          neo_js_context_create_variable(ctx, prop->set, NULL);
      return neo_js_context_call(ctx, setter, receiver ? receiver : object, 1,
                                 &value);
    } else {
      size_t len = wcslen(key->string) + 64;
      wchar_t *message =
          neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
      swprintf(message, len, L"Private field '%ls' is readonly.", key->string);
      neo_js_variable_t error =
          neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
      neo_allocator_free(allocator, message);
      return error;
    }
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_def_private(neo_js_context_t ctx,
                                             neo_js_variable_t object,
                                             neo_js_variable_t field,
                                             neo_js_variable_t value) {
  neo_js_string_t key = neo_js_variable_to_string(field);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  if (!obj->privates) {
    neo_hash_map_initialize_t initialize = {0};
    initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
    initialize.compare = (neo_compare_fn_t)wcscmp;
    initialize.auto_free_key = true;
    initialize.auto_free_value = true;
    obj->privates = neo_create_hash_map(allocator, &initialize);
  }
  if (neo_hash_map_has(obj->privates, key->string, NULL, NULL)) {
    neo_js_string_t str = neo_js_variable_to_string(field);
    size_t len = wcslen(str->string) + 64;
    wchar_t *message =
        neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
    swprintf(message, len, L"Identifier '%ls' has already been declared",
             str->string);
    neo_js_variable_t error =
        neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX, message);
    neo_allocator_free(allocator, message);
    return error;
  }
  neo_js_object_private_t prop = neo_create_js_object_private(allocator);
  prop->value = neo_js_variable_get_handle(value);
  neo_hash_map_set(obj->privates, neo_create_wstring(allocator, key->string),
                   prop, NULL, NULL);
  neo_js_handle_add_parent(prop->value, neo_js_variable_get_handle(object));
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_context_def_private_accessor(
    neo_js_context_t ctx, neo_js_variable_t object, neo_js_variable_t field,
    neo_js_variable_t getter, neo_js_variable_t setter) {
  neo_js_string_t key = neo_js_variable_to_string(field);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  if (!obj->privates) {
    neo_hash_map_initialize_t initialize = {0};
    initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
    initialize.compare = (neo_compare_fn_t)wcscmp;
    initialize.auto_free_key = true;
    initialize.auto_free_value = true;
    obj->privates = neo_create_hash_map(allocator, &initialize);
  }
  neo_js_object_private_t prop =
      neo_hash_map_get(obj->privates, key->string, NULL, NULL);
  if (!prop) {
    prop = neo_create_js_object_private(allocator);
    if (getter) {
      prop->get = neo_js_variable_get_handle(getter);
      neo_js_handle_add_parent(prop->get, neo_js_variable_get_handle(object));
    }
    if (setter) {
      prop->set = neo_js_variable_get_handle(setter);
      neo_js_handle_add_parent(prop->set, neo_js_variable_get_handle(object));
    }
    neo_hash_map_set(obj->privates, neo_create_wstring(allocator, key->string),
                     prop, NULL, NULL);
  } else {
    if (prop->value || (prop->get && getter) || (prop->set && setter)) {
      neo_js_string_t str = neo_js_variable_to_string(field);
      size_t len = wcslen(str->string) + 64;
      wchar_t *message =
          neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
      swprintf(message, len, L"Identifier '%ls' has already been declared",
               str->string);
      neo_js_variable_t error =
          neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX, message);
      neo_allocator_free(allocator, message);
      return error;
    }
    if (getter) {
      prop->get = neo_js_variable_get_handle(getter);
      neo_js_handle_add_parent(prop->get, neo_js_variable_get_handle(object));
    }
    if (setter) {
      prop->set = neo_js_variable_get_handle(setter);
      neo_js_handle_add_parent(prop->set, neo_js_variable_get_handle(object));
    }
  }
  return neo_js_context_create_undefined(ctx);
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
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Object.defineProperty called on non-object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  field = neo_js_context_to_primitive(ctx, field, L"default");
  if (neo_js_variable_get_type(field)->kind != NEO_JS_TYPE_SYMBOL) {
    field = neo_js_context_to_string(ctx, field);
  }
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(ctx, object, field);
  neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  if (!prop) {
    if (obj->sealed || obj->frozen || !obj->extensible) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
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
    if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_STRING &&
        prop->enumerable) {
      neo_list_push(obj->keys, hfield);
    }
    if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
      neo_list_push(obj->symbol_keys, hfield);
    }
  } else {
    if (obj->frozen) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
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
        if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
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
            neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
        neo_allocator_free(allocator, message);
        return error;
      }
      neo_js_handle_remove_parent(prop->value, hobject);
      neo_js_handle_add_parent(prop->value,
                               neo_js_scope_get_root_handle(ctx->scope));
    } else {
      if (!prop->configurable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
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
            neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
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
  if (neo_js_variable_get_type(object)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, L"Object.defineProperty called on non-object");
  }
  neo_js_object_t obj = neo_js_variable_to_object(object);
  field = neo_js_context_to_primitive(ctx, field, L"default");
  if (neo_js_variable_get_type(field)->kind != NEO_JS_TYPE_SYMBOL) {
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
  neo_js_handle_t hfield = neo_js_variable_get_handle(field);
  if (!prop) {
    if (obj->sealed || obj->frozen || !obj->extensible) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
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
    neo_hash_map_set(obj->properties, hfield, prop, ctx, ctx);
    neo_js_handle_add_parent(hfield, hobject);
  } else {
    if (prop->value != NULL) {
      if (!prop->configurable) {
        wchar_t *message = NULL;
        if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
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
            neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
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
        if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
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
            neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
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
    neo_js_handle_t internal =
        neo_hash_map_get(object->internal, field, NULL, NULL);
    if (!internal) {
      return neo_js_context_create_undefined(ctx);
    }
    return neo_js_context_create_variable(ctx, internal, NULL);
  }
  return neo_js_context_create_undefined(ctx);
}

bool neo_js_context_has_internal(neo_js_context_t ctx, neo_js_variable_t self,
                                 const wchar_t *field) {
  neo_js_object_t object = neo_js_variable_to_object(self);
  if (object) {
    neo_js_handle_t internal =
        neo_hash_map_get(object->internal, field, NULL, NULL);
    if (!internal) {
      return false;
    }
    return true;
  }
  return false;
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
  neo_js_type_t type = neo_js_variable_get_type(object);
  neo_js_variable_t result = type->del_field_fn(ctx, object, field);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_get_keys(neo_js_context_t ctx,
                                          neo_js_variable_t variable) {
  variable = neo_js_context_to_object(ctx, variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_ERROR) {
    return variable;
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
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
  result = neo_js_scope_create_variable(current, hresult, NULL);
  neo_js_context_pop_scope(ctx);
  return result;
}

neo_js_variable_t neo_js_context_clone(neo_js_context_t ctx,
                                       neo_js_variable_t self) {
  neo_js_type_t type = neo_js_variable_get_type(self);
  if (type->kind < NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t variable = neo_js_context_create_undefined(ctx);
    neo_js_variable_t error = neo_js_context_copy(ctx, self, variable);
    return variable;
  } else {
    return neo_js_context_create_variable(ctx, neo_js_variable_get_handle(self),
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
    self = neo_js_scope_create_variable(scope, neo_js_variable_get_handle(self),
                                        NULL);
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
    neo_js_scope_create_variable(scope, hvalue, name);
  }
  result = cfunction->callee(ctx, self, argc, argv);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
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
    self = neo_js_context_create_variable(ctx, neo_js_variable_get_handle(self),
                                          NULL);
  }
  neo_js_variable_t clazz = neo_js_callable_get_class(ctx, callee);
  neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL);
  neo_js_scope_set_variable(scope, arguments, L"arguments");
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_context_set_field(ctx, arguments,
                             neo_js_context_create_number(ctx, idx), argv[idx],
                             NULL);
  }
  neo_js_context_set_field(ctx, arguments,
                           neo_js_context_create_string(ctx, L"length"),
                           neo_js_context_create_number(ctx, argc), NULL);

  neo_js_context_set_field(
      ctx, arguments,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"iterator"),
                               NULL),
      neo_js_context_create_cfunction(ctx, L"values", neo_js_array_values),
      NULL);

  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const wchar_t *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_variable(scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  neo_js_vm_t vm = neo_create_js_vm(ctx, self, clazz, function->address, scope);
  neo_js_variable_t coroutine =
      neo_js_context_create_coroutine(ctx, vm, function->program);
  neo_js_context_set_internal(ctx, result, L"[[coroutine]]", coroutine);
  return result;
}

bool neo_js_context_is_thenable(neo_js_context_t ctx,
                                neo_js_variable_t variable) {
  if (neo_js_variable_get_type(variable)->kind < NEO_JS_TYPE_OBJECT) {
    return false;
  }
  neo_js_variable_t then = neo_js_context_get_field(
      ctx, variable, neo_js_context_create_string(ctx, L"then"), NULL);
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
      ctx, neo_create_js_handle(allocator, &coroutine->value), NULL);
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
      ctx, neo_create_js_handle(allocator, &coroutine->value), NULL);
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
static neo_js_variable_t neo_js_awaiter_on_fulfilled(neo_js_context_t ctx,
                                                     neo_js_variable_t self,
                                                     uint32_t argc,
                                                     neo_js_variable_t *argv) {
  neo_js_variable_t coroutine =
      neo_js_context_load_variable(ctx, L"#coroutine");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, L"#task");
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
        ctx, neo_js_variable_get_handle(argv[0]), NULL);
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_context_set_scope(ctx, current);
  if (co_ctx->callee) {
    co_ctx->result = neo_js_variable_get_handle(value);
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
  neo_js_variable_t coroutine =
      neo_js_context_load_variable(ctx, L"#coroutine");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, L"#task");
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
    co_ctx->result = neo_js_variable_get_handle(value);
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

  neo_js_variable_t coroutine =
      neo_js_context_load_variable(ctx, L"#coroutine");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, L"#resolve");
  neo_js_variable_t reject = neo_js_context_load_variable(ctx, L"#reject");
  neo_js_variable_t on_fulfilled =
      neo_js_context_load_variable(ctx, L"#onFulfilled");
  neo_js_variable_t on_rejected =
      neo_js_context_load_variable(ctx, L"#onRejected");
  neo_js_variable_t task = neo_js_context_load_variable(ctx, L"#task");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  neo_list_t stacktrace =
      neo_js_context_set_stacktrace(ctx, co_ctx->stacktrace);
  neo_js_context_push_stackframe(ctx, NULL, L"_.awaiter", 0, 0);
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
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_INTERNAL,
                                              L"Broken coroutine context");
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
          ctx, value, neo_js_context_create_string(ctx, L"then"), NULL);
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
        co_ctx->result = neo_js_variable_get_handle(val);
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
  neo_js_variable_t coroutine =
      neo_js_context_load_variable(ctx, L"#coroutine");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t task =
      neo_js_context_create_cfunction(ctx, NULL, &neo_js_awaiter_task);
  neo_js_variable_t on_fulfilled =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_on_fulfilled);
  neo_js_variable_t on_rejected =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_awaiter_on_rejected);

  neo_js_callable_set_closure(ctx, task, L"#coroutine", coroutine);
  neo_js_callable_set_closure(ctx, task, L"#resolve", resolve);
  neo_js_callable_set_closure(ctx, task, L"#reject", reject);
  neo_js_callable_set_closure(ctx, task, L"#onFulfilled", on_fulfilled);
  neo_js_callable_set_closure(ctx, task, L"#onRejected", on_rejected);
  neo_js_callable_set_closure(ctx, task, L"#task", task);

  neo_js_callable_set_closure(ctx, on_fulfilled, L"#task", task);
  neo_js_callable_set_closure(ctx, on_fulfilled, L"#coroutine", coroutine);

  neo_js_callable_set_closure(ctx, on_rejected, L"#task", task);
  neo_js_callable_set_closure(ctx, on_rejected, L"#coroutine", coroutine);

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
    self = neo_js_context_create_variable(ctx, neo_js_variable_get_handle(self),
                                          NULL);
  }
  neo_js_variable_t clazz = neo_js_callable_get_class(ctx, callee);
  neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL);
  neo_js_scope_set_variable(scope, arguments, L"arguments");
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_context_set_field(ctx, arguments,
                             neo_js_context_create_number(ctx, idx), argv[idx],
                             NULL);
  }
  neo_js_context_set_field(ctx, arguments,
                           neo_js_context_create_string(ctx, L"length"),
                           neo_js_context_create_number(ctx, argc), NULL);

  neo_js_context_set_field(
      ctx, arguments,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"iterator"),
                               NULL),
      neo_js_context_create_cfunction(ctx, L"values", neo_js_array_values),
      NULL);

  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const wchar_t *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_variable(scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  neo_js_vm_t vm = neo_create_js_vm(ctx, self, clazz, function->address, scope);
  neo_js_variable_t coroutine =
      neo_js_context_create_coroutine(ctx, vm, function->program);
  neo_js_callable_set_closure(ctx, resolver, L"#coroutine", coroutine);
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
    self = neo_js_context_create_variable(ctx, neo_js_variable_get_handle(self),
                                          NULL);
  }
  neo_js_variable_t clazz = neo_js_callable_get_class(ctx, callee);
  neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL);
  neo_js_scope_set_variable(scope, arguments, L"arguments");
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_context_set_field(ctx, arguments,
                             neo_js_context_create_number(ctx, idx), argv[idx],
                             NULL);
  }
  neo_js_context_set_field(ctx, arguments,
                           neo_js_context_create_string(ctx, L"length"),
                           neo_js_context_create_number(ctx, argc), NULL);

  neo_js_context_set_field(
      ctx, arguments,
      neo_js_context_get_field(ctx, ctx->std.symbol_constructor,
                               neo_js_context_create_string(ctx, L"iterator"),
                               NULL),
      neo_js_context_create_cfunction(ctx, L"values", neo_js_array_values),
      NULL);

  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const wchar_t *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_variable(scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  neo_js_vm_t vm = neo_create_js_vm(ctx, self, clazz, function->address, scope);
  neo_js_variable_t coroutine =
      neo_js_context_create_coroutine(ctx, vm, function->program);
  neo_js_context_set_internal(ctx, result, L"[[coroutine]]", coroutine);
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
            ctx, neo_js_variable_get_handle(self), NULL);
      }
      neo_js_variable_t clazz = neo_js_callable_get_class(ctx, callee);
      neo_js_variable_t arguments = neo_js_context_create_object(ctx, NULL);
      neo_js_scope_set_variable(scope, arguments, L"arguments");
      for (uint32_t idx = 0; idx < argc; idx++) {
        neo_js_context_set_field(ctx, arguments,
                                 neo_js_context_create_number(ctx, idx),
                                 argv[idx], NULL);
      }
      neo_js_context_set_field(ctx, arguments,
                               neo_js_context_create_string(ctx, L"length"),
                               neo_js_context_create_number(ctx, argc), NULL);

      neo_js_context_set_field(
          ctx, arguments,
          neo_js_context_get_field(
              ctx, ctx->std.symbol_constructor,
              neo_js_context_create_string(ctx, L"iterator"), NULL),
          neo_js_context_create_cfunction(ctx, L"values", neo_js_array_values),
          NULL);
      for (neo_hash_map_node_t it =
               neo_hash_map_get_first(function->callable.closure);
           it != neo_hash_map_get_tail(function->callable.closure);
           it = neo_hash_map_node_next(it)) {
        const wchar_t *name = neo_hash_map_node_get_key(it);
        neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
        neo_js_scope_create_variable(scope, hvalue, name);
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
    self = neo_js_context_create_variable(ctx, neo_js_variable_get_handle(self),
                                          NULL);
  }
  neo_js_variable_t *args =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (uint32_t idx = 0; idx < argc; idx++) {
    args[idx] = neo_js_context_create_variable(
        ctx, neo_js_variable_get_handle(argv[idx]), NULL);
  }
  for (neo_hash_map_node_t it =
           neo_hash_map_get_first(function->callable.closure);
       it != neo_hash_map_get_tail(function->callable.closure);
       it = neo_hash_map_node_next(it)) {
    const wchar_t *name = neo_hash_map_node_get_key(it);
    neo_js_handle_t hvalue = neo_hash_map_node_get_value(it);
    neo_js_scope_create_variable(scope, hvalue, name);
  }
  neo_js_context_set_scope(ctx, current);
  neo_js_variable_t coroutine = neo_js_context_create_native_coroutine(
      ctx, function->callee, bind, argc, args, scope);
  neo_js_callable_set_closure(ctx, resolver, L"#coroutine", coroutine);
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
        ctx, NEO_JS_ERROR_TYPE,
        L"Class constructor cannot be invoked without 'new'");
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
  if (neo_js_variable_get_type(callee)->kind == NEO_JS_TYPE_CFUNCTION) {
    return neo_js_context_call_cfunction(ctx, callee, self, argc, argv);
  } else if (neo_js_variable_get_type(callee)->kind ==
             NEO_JS_TYPE_ASYNC_CFUNCTION) {
    return neo_js_context_call_async_cfunction(ctx, callee, self, argc, argv);
  } else if (neo_js_variable_get_type(callee)->kind == NEO_JS_TYPE_FUNCTION) {
    return neo_js_context_call_function(ctx, callee, self, argc, argv);
  } else {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"callee is not a function");
  }
}

neo_js_variable_t neo_js_context_construct(neo_js_context_t ctx,
                                           neo_js_variable_t constructor,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  if (neo_js_variable_get_type(constructor)->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"variable is not a constructor");
  }
  if (neo_js_variable_get_type(constructor)->kind ==
      NEO_JS_TYPE_ASYNC_CFUNCTION) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"variable is not a constructor");
  }
  if (neo_js_variable_get_type(constructor)->kind == NEO_JS_TYPE_FUNCTION) {
    neo_js_function_t fn = neo_js_variable_to_function(constructor);
    if (fn->is_async || fn->is_generator) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE, L"variable is not a constructor");
    }
  }
  neo_js_call_type_t current_call_type = ctx->call_type;
  ctx->call_type = NEO_JS_CONSTRUCT_CALL;
  neo_js_scope_t current = neo_js_context_get_scope(ctx);
  neo_js_context_push_scope(ctx);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"prototype"), NULL);
  neo_js_variable_t object = neo_js_context_create_object(ctx, prototype);
  neo_js_handle_t hobject = neo_js_variable_get_handle(object);
  neo_js_object_t obj = neo_js_variable_to_object(object);
  obj->constructor = neo_js_variable_get_handle(constructor);
  neo_js_handle_add_parent(obj->constructor, hobject);
  neo_js_context_def_field(ctx, object,
                           neo_js_context_create_string(ctx, L"constructor"),
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
  hobject = neo_js_variable_get_handle(object);
  object = neo_js_scope_create_variable(current, hobject, NULL);
  neo_js_context_pop_scope(ctx);
  ctx->call_type = current_call_type;
  return object;
}

neo_js_variable_t neo_js_context_create_simple_error(neo_js_context_t ctx,
                                                     neo_js_error_type_t type,
                                                     const wchar_t *message) {
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
neo_js_variable_t neo_js_context_create_bigint(neo_js_context_t ctx,
                                               neo_bigint_t value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_bigint_t number = neo_create_js_bigint(allocator, value);
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
                                               neo_js_variable_t prototype) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_object_t object = neo_create_js_object(allocator);
  neo_js_handle_t hobject = neo_create_js_handle(allocator, &object->value);
  if (!prototype) {
    if (ctx->std.object_constructor) {
      prototype = neo_js_context_get_field(
          ctx, ctx->std.object_constructor,
          neo_js_context_create_string(ctx, L"prototype"), NULL);
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

neo_js_variable_t neo_js_context_create_array(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_array_t arr = neo_create_js_array(allocator);
  neo_js_handle_t handle = neo_create_js_handle(allocator, &arr->object.value);
  neo_js_variable_t array = neo_js_context_create_variable(ctx, handle, NULL);
  neo_js_variable_t prototype = NULL;
  if (ctx->std.array_constructor) {
    prototype = neo_js_context_get_field(
        ctx, ctx->std.array_constructor,
        neo_js_context_create_string(ctx, L"prototype"), NULL);
    arr->object.constructor =
        neo_js_variable_get_handle(ctx->std.array_constructor);
  } else {
    prototype = neo_js_context_create_null(ctx);
  }
  arr->object.prototype = neo_js_variable_get_handle(prototype);
  if (arr->object.constructor) {
    neo_js_context_set_field(ctx, array,
                             neo_js_context_create_string(ctx, L"constructor"),
                             ctx->std.array_constructor, NULL);
    neo_js_handle_add_parent(arr->object.constructor, handle);
  }
  neo_js_handle_add_parent(arr->object.prototype, handle);
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
        neo_js_context_create_string(ctx, L"prototype"), NULL));
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
      neo_js_context_create_object(ctx, NULL), true, false, true);
  return result;
}

neo_js_variable_t
neo_js_context_create_async_cfunction(neo_js_context_t ctx, const wchar_t *name,
                                      neo_js_async_cfunction_fn_t cfunction) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(ctx->runtime);
  neo_js_async_cfunction_t func =
      neo_create_js_async_cfunction(allocator, cfunction);
  neo_js_handle_t hfunction =
      neo_create_js_handle(allocator, &func->callable.object.value);
  neo_js_handle_t hproto = NULL;
  hproto = neo_js_variable_get_handle(neo_js_context_get_field(
      ctx, ctx->std.function_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL));
  func->callable.object.prototype = hproto;
  neo_js_handle_add_parent(hproto, hfunction);
  if (name) {
    func->callable.name = neo_create_wstring(allocator, name);
  }
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.async_function_constructor) {
    neo_js_context_def_field(
        ctx, result, neo_js_context_create_string(ctx, L"constructor"),
        ctx->std.async_function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, L"prototype"),
      neo_js_context_create_object(ctx, NULL), true, false, true);
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
        neo_js_context_create_string(ctx, L"prototype"), NULL));
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
      neo_js_context_create_object(ctx, NULL), true, false, true);
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
        neo_js_context_create_string(ctx, L"prototype"), NULL));
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
  neo_js_handle_t hfunction =
      neo_create_js_handle(allocator, &func->callable.object.value);
  neo_js_handle_t hproto = NULL;
  if (ctx->std.async_generator_function_constructor) {
    hproto = neo_js_variable_get_handle(neo_js_context_get_field(
        ctx, ctx->std.async_generator_function_constructor,
        neo_js_context_create_string(ctx, L"prototype"), NULL));
  } else {
    hproto = neo_js_variable_get_handle(neo_js_context_create_null(ctx));
  }
  func->callable.object.prototype = hproto;
  neo_js_handle_add_parent(hproto, hfunction);
  neo_js_variable_t result =
      neo_js_context_create_variable(ctx, hfunction, NULL);
  if (ctx->std.async_generator_function_constructor) {
    neo_js_context_def_field(
        ctx, result, neo_js_context_create_string(ctx, L"constructor"),
        ctx->std.async_generator_function_constructor, true, false, true);
  }
  neo_js_context_def_field(
      ctx, result, neo_js_context_create_string(ctx, L"prototype"),
      neo_js_context_create_object(ctx, NULL), true, false, true);
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
        neo_js_context_create_string(ctx, L"prototype"), NULL));
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
      neo_js_context_create_object(ctx, NULL), true, false, true);
  return result;
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
  neo_js_type_t type = neo_js_variable_get_type(self);
  neo_js_variable_t result = type->to_string_fn(ctx, self);
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
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
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
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
  neo_js_handle_t hresult = neo_js_variable_get_handle(result);
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
      left = neo_js_context_to_primitive(ctx, left, L"default");
      if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
        return left;
      }
      lefttype = neo_js_variable_get_type(left);
    }
    if (righttype->kind >= NEO_JS_TYPE_OBJECT &&
        lefttype->kind < NEO_JS_TYPE_OBJECT) {
      right = neo_js_context_to_primitive(ctx, right, L"default");
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
      const wchar_t *r = neo_js_variable_to_string(right)->string;
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
      const wchar_t *r = neo_js_variable_to_string(left)->string;
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
  left = neo_js_context_to_primitive(ctx, left, L"number");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"number");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING &&
      righttype->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = wcscmp(lstring->string, rstring->string);
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
  left = neo_js_context_to_primitive(ctx, left, L"number");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"number");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING &&
      righttype->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = wcscmp(lstring->string, rstring->string);
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
  left = neo_js_context_to_primitive(ctx, left, L"number");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"number");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING &&
      righttype->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = wcscmp(lstring->string, rstring->string);
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
  left = neo_js_context_to_primitive(ctx, left, L"number");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"number");
  if (neo_js_variable_get_type(right)->kind == NEO_JS_TYPE_ERROR) {
    return right;
  }
  neo_js_type_t lefttype = neo_js_variable_get_type(left);
  neo_js_type_t righttype = neo_js_variable_get_type(right);
  if (lefttype->kind == NEO_JS_TYPE_STRING &&
      righttype->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t lstring = neo_js_variable_to_string(left);
    neo_js_string_t rstring = neo_js_variable_to_string(right);
    int res = wcscmp(lstring->string, rstring->string);
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
    size_t len = wcslen(lstring->string);
    len += wcslen(rstring->string);
    len++;
    wchar_t *str = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
    swprintf(str, len, L"%ls%ls", lstring->string, rstring->string);
    neo_js_variable_t result = neo_js_context_create_string(ctx, str);
    neo_allocator_free(allocator, str);
    return result;
  }
  if (lefttype->kind == NEO_JS_TYPE_BIGINT ||
      righttype->kind == NEO_JS_TYPE_BIGINT) {
    if (lefttype->kind != NEO_JS_TYPE_BIGINT ||
        righttype->kind != NEO_JS_TYPE_BIGINT) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_div(a->bigint, b->bigint);
    if (!res) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE,
                                                L"Division by zero");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
    }
    neo_js_bigint_t a = neo_js_variable_to_bigint(left);
    neo_js_bigint_t b = neo_js_variable_to_bigint(right);
    neo_bigint_t res = neo_bigint_div(a->bigint, b->bigint);
    if (!res) {
      return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE,
                                                L"Division by zero");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
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
      ctx, constructor, neo_js_context_create_string(ctx, L"hasInstance"),
      NULL);
  if (hasInstance &&
      neo_js_variable_get_type(hasInstance)->kind == NEO_JS_TYPE_CFUNCTION) {
    return neo_js_context_call(ctx, hasInstance, constructor, 1, &variable);
  }
  neo_js_object_t object = neo_js_variable_to_object(variable);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, L"prototype"), NULL);
  while (object) {
    if (object == neo_js_variable_to_object(prototype)) {
      return neo_js_context_create_boolean(ctx, true);
    }
    object = neo_js_value_to_object(neo_js_handle_get_value(object->prototype));
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
  if (neo_js_variable_get_type(left)->kind == NEO_JS_TYPE_ERROR) {
    return left;
  }
  right = neo_js_context_to_primitive(ctx, right, L"default");
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
          ctx, NEO_JS_ERROR_TYPE,
          L"Cannot mix BigInt and other types, use explicit conversions");
    }
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"BigInts have no unsigned right shift, use >> instead");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
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
  left = neo_js_context_to_primitive(ctx, left, L"default");
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
  variable = neo_js_context_to_primitive(ctx, variable, L"default");
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
  variable = neo_js_context_to_primitive(ctx, variable, L"default");
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

static neo_js_variable_t neo_js_context_eval_resolver(neo_js_context_t ctx,
                                                      neo_js_variable_t self,
                                                      uint32_t argc,
                                                      neo_js_variable_t *argv) {
  neo_js_variable_t coroutine =
      neo_js_context_load_variable(ctx, L"#coroutine");
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
    neo_js_callable_set_closure(ctx, task, L"#coroutine", coroutine);
    neo_js_callable_set_closure(ctx, task, L"#resolve", resolve);
    neo_js_callable_set_closure(ctx, task, L"#reject", reject);
    neo_js_callable_set_closure(ctx, task, L"#onFulfilled", on_fulfilled);
    neo_js_callable_set_closure(ctx, task, L"#onRejected", on_rejected);
    neo_js_callable_set_closure(ctx, task, L"#task", task);

    neo_js_callable_set_closure(ctx, on_fulfilled, L"#task", task);
    neo_js_callable_set_closure(ctx, on_fulfilled, L"#coroutine", coroutine);

    neo_js_callable_set_closure(ctx, on_rejected, L"#task", task);
    neo_js_callable_set_closure(ctx, on_rejected, L"#coroutine", coroutine);

    neo_js_variable_t then = neo_js_context_get_field(
        ctx, value, neo_js_context_create_string(ctx, L"then"), NULL);

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
  neo_js_variable_t coroutine =
      neo_js_context_load_variable(ctx, L"#coroutine");
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
    neo_js_callable_set_closure(ctx, task, L"#coroutine", coroutine);
    neo_js_callable_set_closure(ctx, task, L"#resolve", resolve);
    neo_js_callable_set_closure(ctx, task, L"#reject", reject);
    neo_js_callable_set_closure(ctx, task, L"#onFulfilled", task);
    neo_js_callable_set_closure(ctx, task, L"#onRejected", on_rejected);
    neo_js_callable_set_closure(ctx, task, L"#task", task);

    neo_js_callable_set_closure(ctx, on_rejected, L"#task", task);
    neo_js_callable_set_closure(ctx, on_rejected, L"#coroutine", coroutine);

    neo_js_variable_t then = neo_js_context_get_field(
        ctx, value, neo_js_context_create_string(ctx, L"then"), NULL);

    neo_js_variable_t args[] = {task, on_rejected};

    return neo_js_context_call(ctx, then, value, 2, args);
  } else {
    return neo_js_awaiter_resolver(ctx, self, argc, argv);
  }
}

neo_js_variable_t neo_js_context_create_compile_error(neo_js_context_t ctx) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_error_t err = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
  wchar_t *message =
      neo_string_to_wstring(allocator, neo_error_get_message(err));
  neo_js_variable_t error =
      neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_SYNTAX, message);
  neo_allocator_free(allocator, message);
  neo_allocator_free(allocator, err);
  return error;
}

void neo_js_context_create_module(neo_js_context_t ctx, const wchar_t *name,
                                  neo_js_variable_t module) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_handle_t hmodule = neo_js_variable_get_handle(module);
  neo_js_handle_add_parent(hmodule, neo_js_scope_get_root_handle(ctx->root));
  neo_hash_map_set(ctx->modules, neo_create_wstring(allocator, name), hmodule,
                   NULL, NULL);
}

neo_js_variable_t neo_js_context_get_module(neo_js_context_t ctx,
                                            const wchar_t *name) {
  neo_js_handle_t hmodule = neo_hash_map_get(ctx->modules, name, NULL, NULL);
  if (!hmodule) {
    size_t len = 64 + wcslen(name);
    neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
    wchar_t *message =
        neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
    swprintf(message, len, L"Cannot find package '%ls'", name);
    neo_js_variable_t result = neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_REFERENCE, message);
    neo_allocator_free(allocator, message);
    return result;
  }
  return neo_js_context_create_variable(ctx, hmodule, NULL);
}

bool neo_js_context_has_module(neo_js_context_t ctx, const wchar_t *name) {
  neo_js_handle_t hmodule = neo_hash_map_get(ctx->modules, name, NULL, NULL);
  if (!hmodule) {
    return false;
  }
  return true;
}

neo_js_variable_t neo_js_context_eval(neo_js_context_t ctx, const wchar_t *file,
                                      const char *source) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_path_t path = neo_create_path(allocator, file);
  path = neo_path_absolute(path);
  wchar_t *filepath = neo_path_to_string(path);
  const wchar_t *path_ext_name = neo_path_extname(path);
  wchar_t *ext_name = NULL;
  if (path_ext_name) {
    ext_name = neo_wstring_to_lower(allocator, path_ext_name);
  }
  neo_allocator_free(allocator, path);
  if (ext_name && wcscmp(ext_name, L".json") == 0) {
    neo_allocator_free(allocator, ext_name);
    neo_position_t position = {0, 0, source};
    neo_js_variable_t def =
        neo_js_json_read_variable(ctx, &position, NULL, filepath);
    NEO_JS_TRY_AND_THROW(def);
    neo_js_variable_t module =
        neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));
    neo_js_handle_t hmodule = neo_js_variable_get_handle(module);
    neo_js_handle_add_parent(hmodule, neo_js_scope_get_root_handle(ctx->root));
    neo_js_context_set_field(
        ctx, module, neo_js_context_create_string(ctx, L"default"), def, NULL);
    neo_hash_map_set(ctx->modules, neo_create_wstring(allocator, filepath),
                     hmodule, NULL, NULL);
    neo_allocator_free(allocator, filepath);
    return module;
  }
  neo_allocator_free(allocator, ext_name);
  neo_program_t program = neo_js_runtime_get_program(ctx->runtime, filepath);
  if (!program) {
    neo_js_variable_t module =
        neo_js_context_create_object(ctx, neo_js_context_create_null(ctx));

    neo_js_handle_t hmodule = neo_js_variable_get_handle(module);
    neo_js_handle_add_parent(hmodule, neo_js_scope_get_root_handle(ctx->root));
    neo_hash_map_set(ctx->modules, neo_create_wstring(allocator, filepath),
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
    wchar_t *asmpath = neo_create_wstring(allocator, filepath);
    size_t max = wcslen(asmpath) + 1;
    asmpath = neo_wstring_concat(allocator, asmpath, &max, L".asm");
    char *casmpath = neo_wstring_to_string(allocator, asmpath);
    neo_allocator_free(allocator, asmpath);
    FILE *fp = fopen(casmpath, "w");
    TRY(neo_program_write(allocator, fp, program)) {
      neo_allocator_free(allocator, root);
      neo_allocator_free(allocator, filepath);
      return neo_js_context_create_compile_error(ctx);
    };
    fclose(fp);
    neo_allocator_free(allocator, casmpath);
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
    neo_js_callable_set_closure(ctx, resolver, L"#coroutine", coroutine);
    return neo_js_context_construct(ctx, ctx->std.promise_constructor, 1,
                                    &resolver);
  } else {
    neo_allocator_free(allocator, scope);
    neo_allocator_free(allocator, vm);
    return result;
  }
}
neo_js_variable_t neo_js_context_assert(neo_js_context_t ctx,
                                        const wchar_t *type,
                                        const wchar_t *value,
                                        const wchar_t *file) {
  return ctx->assert_fn(ctx, type, value, file);
}

neo_js_assert_fn_t neo_js_context_set_assert_fn(neo_js_context_t ctx,
                                                neo_js_assert_fn_t assert_fn) {
  neo_js_assert_fn_t current = ctx->assert_fn;
  ctx->assert_fn = assert_fn;
  return current;
}

void neo_js_context_enable(neo_js_context_t ctx, const wchar_t *feature) {
  neo_js_feature_t feat = neo_hash_map_get(ctx->features, feature, NULL, NULL);
  if (feat && feat->enable_fn) {
    feat->enable_fn(ctx, feature);
  }
}

void neo_js_context_disable(neo_js_context_t ctx, const wchar_t *feature) {
  neo_js_feature_t feat = neo_hash_map_get(ctx->features, feature, NULL, NULL);
  if (feat && feat->disable_fn) {
    feat->disable_fn(ctx, feature);
  }
}

void neo_js_context_set_feature(neo_js_context_t ctx, const wchar_t *feature,
                                neo_js_feature_fn_t enable_fn,
                                neo_js_feature_fn_t disable_fn) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_feature_t feat =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_feature_t), NULL);
  feat->enable_fn = enable_fn;
  feat->disable_fn = disable_fn;
  neo_hash_map_set(ctx->features, neo_create_wstring(allocator, feature), feat,
                   NULL, NULL);
}
neo_js_call_type_t neo_js_context_get_call_type(neo_js_context_t ctx) {
  return ctx->call_type;
}