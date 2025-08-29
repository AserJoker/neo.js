#ifndef _H_NEO_ENGINE_STD_SYMBOL_
#define _H_NEO_ENGINE_STD_SYMBOL_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
neo_js_variable_t neo_js_symbol_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv);

neo_js_variable_t neo_js_symbol_to_string(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv);

neo_js_variable_t neo_js_symbol_value_of(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);

neo_js_variable_t neo_js_symbol_to_primitive(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv);

neo_js_variable_t neo_js_symbol_for(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_symbol_key_for(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv);

void neo_js_context_init_std_symbol(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif