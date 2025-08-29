#ifndef _H_NEO_ENGINE_STD_BOOLEAN_
#define _H_NEO_ENGINE_STD_BOOLEAN_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_js_variable_t neo_js_boolean_constructor(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv);

neo_js_variable_t neo_js_boolean_to_string(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv);

neo_js_variable_t neo_js_boolean_value_of(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv);

void neo_js_context_init_std_boolean(neo_js_context_t ctx);

#ifdef __cplusplus
}
#endif
#endif