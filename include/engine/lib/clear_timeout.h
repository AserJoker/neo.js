#ifndef _H_NEO_LIB_CLEAR_TIMEOUT_
#define _H_NEO_LIB_CLEAR_TIMEOUT_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
neo_js_variable_t neo_js_clear_timeout(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv);
#ifdef __cplusplus
}
#endif
#endif