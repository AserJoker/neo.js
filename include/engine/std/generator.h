#ifndef _H_NEO_ENGINE_STD_GENERATOR_
#define _H_NEO_ENGINE_STD_GENERATOR_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
neo_js_variable_t neo_js_generator_iterator(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv);
                                            
neo_js_variable_t neo_js_generator_constructor(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv);

neo_js_variable_t neo_js_generator_next(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv);

neo_js_variable_t neo_js_generator_return(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv);

neo_js_variable_t neo_js_generator_throw(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);
#ifdef __cplusplus
}
#endif
#endif