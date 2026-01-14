#ifndef _H_NEO_RUNTIME_CONSOLE_
#define _H_NEO_RUNTIME_CONSOLE_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_console_constructor);
NEO_JS_CFUNCTION(neo_js_console_log);
void neo_initialize_js_console(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif