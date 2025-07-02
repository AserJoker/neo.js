#ifndef _H_NEO_ENGINE_STD_PROMISE_
#define _H_NEO_ENGINE_STD_PROMISE_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_js_variable_t neo_js_promise_constructor(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv);

neo_js_variable_t neo_js_promise_resolve(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);

neo_js_variable_t neo_js_promise_reject(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv);

neo_js_variable_t neo_js_promise_all(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv);

neo_js_variable_t neo_js_promise_race(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_promise_then(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_promise_catch(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv);

neo_js_variable_t neo_js_promise_finally(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);

#ifdef __cplusplus
}
#endif
#endif