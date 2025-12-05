#include "runtime/constant.h"
#include "engine/boolean.h"
#include "engine/context.h"
#include "engine/null.h"
#include "engine/number.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/undefined.h"
#include "engine/uninitialized.h"
#include "runtime/array.h"
#include "runtime/array_iterator.h"
#include "runtime/async_function.h"
#include "runtime/async_generator.h"
#include "runtime/async_generator_function.h"
#include "runtime/error.h"
#include "runtime/function.h"
#include "runtime/generator.h"
#include "runtime/generator_function.h"
#include "runtime/iterator.h"
#include "runtime/object.h"
#include "runtime/promise.h"
#include "runtime/range_error.h"
#include "runtime/reference_error.h"
#include "runtime/symbol.h"
#include "runtime/syntax_error.h"
#include "runtime/time.h"
#include "runtime/type_error.h"
#include <math.h>

void neo_initialize_js_constant(neo_js_context_t ctx) {
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  constant->uninitialized = neo_js_context_create_variable(
      ctx, &neo_create_js_uninitialized(allocator)->super);
  constant->undefined = neo_js_context_create_variable(
      ctx, &neo_create_js_undefined(allocator)->super);
  constant->null = neo_js_context_create_variable(
      ctx, &neo_create_js_null(allocator)->super);
  constant->nan = neo_js_context_create_variable(
      ctx, &neo_create_js_number(allocator, NAN)->super);
  constant->infinity = neo_js_context_create_variable(
      ctx, &neo_create_js_number(allocator, INFINITY)->super);
  constant->boolean_true = neo_js_context_create_variable(
      ctx, &neo_create_js_boolean(allocator, true)->super);
  constant->boolean_false = neo_js_context_create_variable(
      ctx, &neo_create_js_boolean(allocator, false)->super);
  constant->key_name = neo_js_context_create_cstring(ctx, "name");
  constant->key_constructor = neo_js_context_create_cstring(ctx, "constructor");
  constant->key_prototype = neo_js_context_create_cstring(ctx, "prototype");
  neo_initialize_js_object(ctx);
  neo_initialize_js_function(ctx);
  constant->global = neo_js_context_create_object(ctx, NULL);
  neo_initialize_js_symbol(ctx);
  neo_initialize_js_array(ctx);
  neo_initialize_js_iterator(ctx);
  neo_initialize_js_array_iterator(ctx);
  neo_initialize_js_error(ctx);
  neo_initialize_js_type_error(ctx);
  neo_initialize_js_syntax_error(ctx);
  neo_initialize_js_reference_error(ctx);
  neo_initialize_js_range_error(ctx);
  neo_initialize_js_generator_function(ctx);
  neo_initialize_js_generator(ctx);
  neo_initialize_js_async_function(ctx);
  neo_initialize_js_async_generator_function(ctx);
  neo_initialize_js_async_generator(ctx);
  neo_initialize_js_promise(ctx);
  neo_initialize_js_time(ctx);

  neo_js_scope_set_variable(root_scope, constant->global, NULL);
  neo_js_scope_set_variable(root_scope, constant->uninitialized, NULL);
  neo_js_scope_set_variable(root_scope, constant->undefined, NULL);
  neo_js_scope_set_variable(root_scope, constant->null, NULL);
  neo_js_scope_set_variable(root_scope, constant->nan, NULL);
  neo_js_scope_set_variable(root_scope, constant->infinity, NULL);
  neo_js_scope_set_variable(root_scope, constant->boolean_true, NULL);
  neo_js_scope_set_variable(root_scope, constant->boolean_false, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_name, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_constructor, NULL);
  neo_js_scope_set_variable(root_scope, constant->key_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->object_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->object_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->array_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->array_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->function_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->function_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_async_dispose, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_async_iterator, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_has_instance, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_is_concat_spreadable,
                            NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_iterator, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_match, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_match_all, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_replace, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_search, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_species, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_split, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_to_primitive, NULL);
  neo_js_scope_set_variable(root_scope, constant->symbol_to_string_tag, NULL);
  neo_js_scope_set_variable(root_scope, constant->iterator_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->iterator_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->array_iterator_prototype,
                            NULL);
  neo_js_scope_set_variable(root_scope, constant->error_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->type_error_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->syntax_error_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->reference_error_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->range_error_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->generator_function_class,
                            NULL);
  neo_js_scope_set_variable(root_scope, constant->generator_function_prototype,
                            NULL);
  neo_js_scope_set_variable(root_scope,
                            constant->async_generator_function_class, NULL);
  neo_js_scope_set_variable(root_scope,
                            constant->async_generator_function_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->async_function_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->async_function_prototype,
                            NULL);
  neo_js_scope_set_variable(root_scope, constant->generator_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->async_generator_prototype,
                            NULL);
  neo_js_scope_set_variable(root_scope, constant->promise_class, NULL);
  neo_js_scope_set_variable(root_scope, constant->promise_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->set_timeout, NULL);
  neo_js_scope_set_variable(root_scope, constant->clear_timeout, NULL);
  neo_js_scope_set_variable(root_scope, constant->set_interval, NULL);
  neo_js_scope_set_variable(root_scope, constant->clear_interval, NULL);

  NEO_JS_DEF_FIELD(ctx, constant->global, "global", constant->global);
  NEO_JS_DEF_FIELD(ctx, constant->global, "undefined", constant->undefined);
  NEO_JS_DEF_FIELD(ctx, constant->global, "NaN", constant->nan);
  NEO_JS_DEF_FIELD(ctx, constant->global, "Infinity", constant->infinity);
  NEO_JS_DEF_FIELD(ctx, constant->global, "Object", constant->object_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "Function", constant->function_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "Array", constant->array_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "Iterator", constant->iterator_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "Symbol", constant->symbol_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "Error", constant->error_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "TypeError",
                   constant->type_error_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "SyntaxError",
                   constant->syntax_error_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "ReferenceError",
                   constant->reference_error_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "RangeError",
                   constant->range_error_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "Promise", constant->promise_class);
  NEO_JS_DEF_FIELD(ctx, constant->global, "setTimeout", constant->set_timeout);
  NEO_JS_DEF_FIELD(ctx, constant->global, "clearTimeout",
                   constant->clear_timeout);
  NEO_JS_DEF_FIELD(ctx, constant->global, "setInterval",
                   constant->set_interval);
  NEO_JS_DEF_FIELD(ctx, constant->global, "clearInterval",
                   constant->clear_interval);
}