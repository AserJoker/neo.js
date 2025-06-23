#ifndef _H_NEO_ENGINE_STD_ARRAY_
#define _H_NEO_ENGINE_STD_ARRAY_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_engine_variable_t neo_engine_array_constructor(neo_engine_context_t ctx,
                                                   neo_engine_variable_t self,
                                                   uint32_t argc,
                                                   neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_array_to_string(neo_engine_context_t ctx,
                                                 neo_engine_variable_t self,
                                                 uint32_t argc,
                                                 neo_engine_variable_t *argv);
#ifdef __cplusplus
}
#endif
#endif