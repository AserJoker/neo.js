#ifndef _H_NEO_JS_STD_OBJECT_
#define _H_NEO_JS_STD_OBJECT_
#include "js/basetype/object.h"
#include "js/context.h"
#include "js/type.h"
#include "js/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
neo_js_variable_t neo_js_object_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv);

neo_js_variable_t neo_js_object_keys(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv);

neo_js_variable_t neo_js_object_value_of(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);

neo_js_variable_t neo_js_object_to_string(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv);

neo_js_variable_t neo_js_object_to_local_string(neo_js_context_t ctx,
                                                neo_js_variable_t self,
                                                uint32_t argc,
                                                neo_js_variable_t *argv);

neo_js_variable_t neo_js_object_has_own_property(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 uint32_t argc,
                                                 neo_js_variable_t *argv);

neo_js_variable_t neo_js_object_is_prototype_of(neo_js_context_t ctx,
                                                neo_js_variable_t self,
                                                uint32_t argc,
                                                neo_js_variable_t *argv);

neo_js_variable_t neo_js_object_property_is_enumerable(neo_js_context_t ctx,
                                                       neo_js_variable_t self,
                                                       uint32_t argc,
                                                       neo_js_variable_t *argv);
#ifdef __cplusplus
}
#endif
#endif