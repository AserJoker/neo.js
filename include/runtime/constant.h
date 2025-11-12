#ifndef _H_NEO_RUNTIME_CONSTANT_
#define _H_NEO_RUNTIME_CONSTANT_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_constant_t {
  // global
  neo_js_variable_t global;
  // key
  neo_js_variable_t key_name;
  neo_js_variable_t key_prototype;
  neo_js_variable_t key_constructor;
  // base
  neo_js_variable_t uninitialized;
  neo_js_variable_t undefined;
  neo_js_variable_t null;
  neo_js_variable_t nan;
  neo_js_variable_t infinity;
  neo_js_variable_t boolean_true;
  neo_js_variable_t boolean_false;
  // function
  neo_js_variable_t function_prototype;
  neo_js_variable_t function_class;
  // object
  neo_js_variable_t object_prototype;
  neo_js_variable_t object_class;
  // array
  neo_js_variable_t array_prototype;
  neo_js_variable_t array_class;
  // iterator
  neo_js_variable_t iterator_class;
  neo_js_variable_t iterator_prototype;
  // array_iterator
  neo_js_variable_t array_iterator_prototype;
  // symbol
  neo_js_variable_t symbol_prototype;
  neo_js_variable_t symbol_class;
  neo_js_variable_t symbol_for;
  neo_js_variable_t symbol_key_for;
  neo_js_variable_t symbol_async_dispose;
  neo_js_variable_t symbol_async_iterator;
  neo_js_variable_t symbol_iterator;
  neo_js_variable_t symbol_match;
  neo_js_variable_t symbol_match_all;
  neo_js_variable_t symbol_replace;
  neo_js_variable_t symbol_search;
  neo_js_variable_t symbol_species;
  neo_js_variable_t symbol_split;
  neo_js_variable_t symbol_to_primitive;
  neo_js_variable_t symbol_to_string_tag;
  // error
  neo_js_variable_t error_class;
  // type_error
  neo_js_variable_t type_error_class;
  // syntax_error
  neo_js_variable_t syntax_error_class;
  // reference_error
  neo_js_variable_t reference_error_class;
  // reference_error
  neo_js_variable_t range_error_class;
};
typedef struct _neo_js_constant_t *neo_js_constant_t;
void neo_initialize_js_constant(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif