#ifndef _H_NEO_ENGINE_STD_REGEXP_
#define _H_NEO_ENGINE_STD_REGEXP_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NEO_REGEXP_FLAG_GLOBAL (1 << 1)
#define NEO_REGEXP_FLAG_IGNORECASE (1 << 2)
#define NEO_REGEXP_FLAG_MULTILINE (1 << 3)
#define NEO_REGEXP_FLAG_DOTALL (1 << 4)
#define NEO_REGEXP_FLAG_UNICODE (1 << 5)
#define NEO_REGEXP_FLAG_STICKY (1 << 6)
#define NEO_REGEXP_FLAG_HAS_INDICES (1 << 7)

uint8_t neo_js_regexp_get_flag(neo_js_context_t ctx, neo_js_variable_t self);

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

void neo_js_context_init_std_regexp(neo_js_context_t ctx);

#ifdef __cplusplus
}
#endif
#endif