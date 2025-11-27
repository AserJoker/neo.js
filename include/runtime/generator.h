#ifndef _H_NEO_RUNTIME_GENERATOR_
#define _H_NEO_RUNTIME_GENERATOR_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_generator_next);
NEO_JS_CFUNCTION(neo_js_generator_return);
NEO_JS_CFUNCTION(neo_js_generator_throw);
void neo_initialize_js_generator(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif