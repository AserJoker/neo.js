#ifndef _H_NEO_ENGINE_CFUNCTION_
#define _H_NEO_ENGINE_CFUNCTION_
#include "core/allocator.h"
#include "engine/function.h"
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_cfunction_t {
  struct _neo_js_function_t super;
  neo_js_cfunc_t callee;
};
typedef struct _neo_js_cfunction_t *neo_js_cfunction_t;
neo_js_cfunction_t neo_create_js_cfunction(neo_allocator_t allocator,
                                           neo_js_cfunc_t callee,
                                           neo_js_value_t prototype);
void neo_init_js_cfunction(neo_js_cfunction_t self, neo_allocator_t allocaotr,
                           neo_js_cfunc_t callee, neo_js_value_t prototype);
void neo_deinit_js_cfunction(neo_js_cfunction_t self,
                             neo_allocator_t allocaotr);
neo_js_value_t neo_js_cfunction_to_value(neo_js_cfunction_t self);

#ifdef __cplusplus
}
#endif
#endif