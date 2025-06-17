#ifndef _H_NEO_JS_STD_OBJECT_
#define _H_NEO_JS_STD_OBJECT_
#include "js/type.h"
#ifdef __cplusplus
extern "C" {
#endif
neo_js_variable_t neo_js_object_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv);
#ifdef __cplusplus
}
#endif
#endif