#ifndef _H_NEO_ENGINE_STD_AGGREGATE_ERROR_
#define _H_NEO_ENGINE_STD_AGGREGATE_ERROR_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

NEO_JS_CFUNCTION(neo_js_aggregate_error_constructor);
void neo_js_context_init_std_aggregate_error(neo_js_context_t ctx);

#ifdef __cplusplus
}
#endif
#endif