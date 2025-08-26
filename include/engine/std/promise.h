#ifndef _H_NEO_ENGINE_STD_PROMISE_
#define _H_NEO_ENGINE_STD_PROMISE_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

NEO_JS_CFUNCTION(neo_js_promise_all);
NEO_JS_CFUNCTION(neo_js_promise_all_settled);
NEO_JS_CFUNCTION(neo_js_promise_any);
NEO_JS_CFUNCTION(neo_js_promise_race);
NEO_JS_CFUNCTION(neo_js_promise_resolve);
NEO_JS_CFUNCTION(neo_js_promise_reject);
NEO_JS_CFUNCTION(neo_js_promise_try);
NEO_JS_CFUNCTION(neo_js_promise_with_resolvers);
NEO_JS_CFUNCTION(neo_js_promise_species);
NEO_JS_CFUNCTION(neo_js_promise_constructor);
NEO_JS_CFUNCTION(neo_js_promise_then);
NEO_JS_CFUNCTION(neo_js_promise_catch);
NEO_JS_CFUNCTION(neo_js_promise_finally);

#ifdef __cplusplus
}
#endif
#endif