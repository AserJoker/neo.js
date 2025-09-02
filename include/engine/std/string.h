#ifndef _H_NEO_ENGINE_STD_STRING_
#define _H_NEO_ENGINE_STD_STRING_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_string_from_char_code);
NEO_JS_CFUNCTION(neo_js_string_from_code_point);
NEO_JS_CFUNCTION(neo_js_string_raw);
NEO_JS_CFUNCTION(neo_js_string_constructor);
NEO_JS_CFUNCTION(neo_js_string_at);
NEO_JS_CFUNCTION(neo_js_string_char_at);
NEO_JS_CFUNCTION(neo_js_string_char_code_at);
NEO_JS_CFUNCTION(neo_js_string_char_point_at);
NEO_JS_CFUNCTION(neo_js_string_concat);
NEO_JS_CFUNCTION(neo_js_string_ends_with);
NEO_JS_CFUNCTION(neo_js_string_includes);
NEO_JS_CFUNCTION(neo_js_string_index_of);
NEO_JS_CFUNCTION(neo_js_string_is_well_formed);
NEO_JS_CFUNCTION(neo_js_string_last_index_of);
NEO_JS_CFUNCTION(neo_js_string_local_compare);
NEO_JS_CFUNCTION(neo_js_string_match);
NEO_JS_CFUNCTION(neo_js_string_match_all);
NEO_JS_CFUNCTION(neo_js_string_normalize);
NEO_JS_CFUNCTION(neo_js_string_pad_end);
NEO_JS_CFUNCTION(neo_js_string_pad_start);
NEO_JS_CFUNCTION(neo_js_string_repeat);
NEO_JS_CFUNCTION(neo_js_string_replace);
NEO_JS_CFUNCTION(neo_js_string_replace_all);
NEO_JS_CFUNCTION(neo_js_string_search);
NEO_JS_CFUNCTION(neo_js_string_slice);
NEO_JS_CFUNCTION(neo_js_string_split);
NEO_JS_CFUNCTION(neo_js_string_starts_with);
NEO_JS_CFUNCTION(neo_js_string_substring);
NEO_JS_CFUNCTION(neo_js_string_to_local_lower_case);
NEO_JS_CFUNCTION(neo_js_string_to_local_upper_case);
NEO_JS_CFUNCTION(neo_js_string_to_lower_case);
NEO_JS_CFUNCTION(neo_js_string_to_string);
NEO_JS_CFUNCTION(neo_js_string_to_upper_case);
NEO_JS_CFUNCTION(neo_js_string_to_well_formed);
NEO_JS_CFUNCTION(neo_js_string_trim);
NEO_JS_CFUNCTION(neo_js_string_trim_end);
NEO_JS_CFUNCTION(neo_js_string_trim_start);
NEO_JS_CFUNCTION(neo_js_string_value_of);
NEO_JS_CFUNCTION(neo_js_string_iterator);
void neo_js_context_init_std_string(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif