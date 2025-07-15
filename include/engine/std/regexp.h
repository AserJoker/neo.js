#ifndef _H_NEO_ENGINE_STD_REGEXP_
#define _H_NEO_ENGINE_STD_REGEXP_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_js_variable_t neo_js_regexp_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv);

neo_js_variable_t neo_js_regexp_to_string(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv);

neo_js_variable_t neo_js_regexp_exec(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv);

neo_js_variable_t neo_js_regexp_test(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv);

#ifdef __cplusplus
}
#endif
#endif