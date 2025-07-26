#ifndef _H_NEO_LIB_JSON_
#define _H_NEO_LIB_JSON_
#include "core/position.h"
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
neo_js_variable_t neo_js_json_read_variable(neo_js_context_t ctx,
                                            neo_position_t *position,
                                            neo_js_variable_t receiver,
                                            const wchar_t *file);
NEO_JS_CFUNCTION(neo_js_json_stringify);
NEO_JS_CFUNCTION(neo_js_json_parse);
#ifdef __cplusplus
}
#endif
#endif