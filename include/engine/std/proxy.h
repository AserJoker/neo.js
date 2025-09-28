#ifndef _H_NEO_ENGINE_STD_PROXY_
#define _H_NEO_ENGINE_STD_PROXY_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_proxy_constructor);
void neo_js_context_init_std_proxy(neo_js_context_t ctx);

#ifdef __cplusplus
}
#endif
#endif