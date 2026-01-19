#ifndef _H_NEO_RUNTIME_ARRAY_
#define _H_NEO_RUNTIME_ARRAY_
#include "neojs/engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_array_from);
NEO_JS_CFUNCTION(neo_js_array_from_async);
NEO_JS_CFUNCTION(neo_js_array_is_array);
NEO_JS_CFUNCTION(neo_js_array_of);
NEO_JS_CFUNCTION(neo_js_array_constructor);
NEO_JS_CFUNCTION(neo_js_array_at);
NEO_JS_CFUNCTION(neo_js_array_concat);
NEO_JS_CFUNCTION(neo_js_array_copy_within);
NEO_JS_CFUNCTION(neo_js_array_entries);
NEO_JS_CFUNCTION(neo_js_array_every);
NEO_JS_CFUNCTION(neo_js_array_fill);
NEO_JS_CFUNCTION(neo_js_array_filter);
NEO_JS_CFUNCTION(neo_js_array_find);
NEO_JS_CFUNCTION(neo_js_array_find_index);
NEO_JS_CFUNCTION(neo_js_array_find_last);
NEO_JS_CFUNCTION(neo_js_array_find_last_index);
NEO_JS_CFUNCTION(neo_js_array_flat);
NEO_JS_CFUNCTION(neo_js_array_flat_map);
NEO_JS_CFUNCTION(neo_js_array_for_each);
NEO_JS_CFUNCTION(neo_js_array_includes);
NEO_JS_CFUNCTION(neo_js_array_index_of);
NEO_JS_CFUNCTION(neo_js_array_join);
NEO_JS_CFUNCTION(neo_js_array_keys);
NEO_JS_CFUNCTION(neo_js_array_last_index_of);
NEO_JS_CFUNCTION(neo_js_array_map);
NEO_JS_CFUNCTION(neo_js_array_pop);
NEO_JS_CFUNCTION(neo_js_array_push);
NEO_JS_CFUNCTION(neo_js_array_reduce);
NEO_JS_CFUNCTION(neo_js_array_reduce_right);
NEO_JS_CFUNCTION(neo_js_array_reverse);
NEO_JS_CFUNCTION(neo_js_array_shift);
NEO_JS_CFUNCTION(neo_js_array_slice);
NEO_JS_CFUNCTION(neo_js_array_some);
NEO_JS_CFUNCTION(neo_js_array_sort);
NEO_JS_CFUNCTION(neo_js_array_splice);
NEO_JS_CFUNCTION(neo_js_array_to_locale_string);
NEO_JS_CFUNCTION(neo_js_array_to_reversed);
NEO_JS_CFUNCTION(neo_js_array_to_sorted);
NEO_JS_CFUNCTION(neo_js_array_to_spliced);
NEO_JS_CFUNCTION(neo_js_array_to_string);
NEO_JS_CFUNCTION(neo_js_array_unshift);
NEO_JS_CFUNCTION(neo_js_array_values);
NEO_JS_CFUNCTION(neo_js_array_with);
NEO_JS_CFUNCTION(neo_js_array_get_symbol_species);
void neo_initialize_js_array(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif