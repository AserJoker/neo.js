#ifndef _H_NEO_RUNTIME_PROMISE_
#define _H_NEO_RUNTIME_PROMISE_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif

struct _neo_js_promise_t {
  neo_list_t onfulfilled;
  neo_list_t onrejected;
  neo_js_value_t value;
};

typedef struct _neo_js_promise_t *neo_js_promise_t;
NEO_JS_CFUNCTION(neo_js_promise_resolve);
NEO_JS_CFUNCTION(neo_js_promise_reject);
NEO_JS_CFUNCTION(neo_js_promise_constructor);
NEO_JS_CFUNCTION(neo_js_promise_then);
NEO_JS_CFUNCTION(neo_js_promise_catch);
NEO_JS_CFUNCTION(neo_js_promise_finally);
void neo_initialize_js_promise(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif