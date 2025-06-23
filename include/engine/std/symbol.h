#ifndef _H_NEO_ENGINE_STD_SYMBOL_
#define _H_NEO_ENGINE_STD_SYMBOL_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
neo_engine_variable_t
neo_engine_symbol_constructor(neo_engine_context_t ctx,
                              neo_engine_variable_t self, uint32_t argc,
                              neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_symbol_to_string(neo_engine_context_t ctx,
                                                  neo_engine_variable_t self,
                                                  uint32_t argc,
                                                  neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_symbol_value_of(neo_engine_context_t ctx,
                                                 neo_engine_variable_t self,
                                                 uint32_t argc,
                                                 neo_engine_variable_t *argv);

neo_engine_variable_t
neo_engine_symbol_to_primitive(neo_engine_context_t ctx,
                               neo_engine_variable_t self, uint32_t argc,
                               neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_symbol_for(neo_engine_context_t ctx,
                                            neo_engine_variable_t self,
                                            uint32_t argc,
                                            neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_symbol_key_for(neo_engine_context_t ctx,
                                                neo_engine_variable_t self,
                                                uint32_t argc,
                                                neo_engine_variable_t *argv);
#ifdef __cplusplus
}
#endif
#endif