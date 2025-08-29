#ifndef _H_NEO_ENGINE_STD_ERROR_
#define _H_NEO_ENGINE_STD_ERROR_
#include "core/list.h"
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_error_info_t *neo_js_error_info_t;
struct _neo_js_error_info_t {
  const wchar_t *type;
  neo_list_t stacktrace;
};
neo_js_variable_t neo_js_error_constructor(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv);

neo_js_variable_t neo_js_error_to_string(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);
void neo_js_context_init_std_error(neo_js_context_t ctx);

#ifdef __cplusplus
}
#endif
#endif