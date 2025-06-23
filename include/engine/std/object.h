#ifndef _H_NEO_ENGINE_STD_OBJECT_
#define _H_NEO_ENGINE_STD_OBJECT_
#include "engine/basetype/object.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
neo_engine_variable_t
neo_engine_object_constructor(neo_engine_context_t ctx,
                              neo_engine_variable_t self, uint32_t argc,
                              neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_object_keys(neo_engine_context_t ctx,
                                             neo_engine_variable_t self,
                                             uint32_t argc,
                                             neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_object_value_of(neo_engine_context_t ctx,
                                                 neo_engine_variable_t self,
                                                 uint32_t argc,
                                                 neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_object_to_string(neo_engine_context_t ctx,
                                                  neo_engine_variable_t self,
                                                  uint32_t argc,
                                                  neo_engine_variable_t *argv);

neo_engine_variable_t
neo_engine_object_to_local_string(neo_engine_context_t ctx,
                                  neo_engine_variable_t self, uint32_t argc,
                                  neo_engine_variable_t *argv);

neo_engine_variable_t
neo_engine_object_has_own_property(neo_engine_context_t ctx,
                                   neo_engine_variable_t self, uint32_t argc,
                                   neo_engine_variable_t *argv);

neo_engine_variable_t
neo_engine_object_is_prototype_of(neo_engine_context_t ctx,
                                  neo_engine_variable_t self, uint32_t argc,
                                  neo_engine_variable_t *argv);

neo_engine_variable_t neo_engine_object_property_is_enumerable(
    neo_engine_context_t ctx, neo_engine_variable_t self, uint32_t argc,
    neo_engine_variable_t *argv);
#ifdef __cplusplus
}
#endif
#endif