#ifndef _H_NEO_ENGINE_STD_ARRAY_ITERATOR_
#define _H_NEO_ENGINE_STD_ARRAY_ITERATOR_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_js_variable_t neo_js_array_iterator_constructor(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    uint32_t argc,
                                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_iterator_next(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_iterator_iterator(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 uint32_t argc,
                                                 neo_js_variable_t *argv);
#ifdef __cplusplus
}
#endif
#endif