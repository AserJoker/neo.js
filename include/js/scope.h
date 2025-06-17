#ifndef _H_NEO_JS_SCOPE_
#define _H_NEO_JS_SCOPE_
#include "core/allocator.h"
#include "js/handle.h"
#include "js/type.h"
#include "js/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_scope_t *neo_js_scope_t;

neo_js_scope_t neo_create_js_scope(neo_allocator_t allocator,
                                   neo_js_scope_t parent);

uint32_t neo_js_scope_add_ref(neo_js_scope_t self);

uint32_t neo_js_scope_ref(neo_js_scope_t self);

uint32_t neo_js_scope_release(neo_js_scope_t self);

neo_js_scope_t neo_js_scope_get_parent(neo_js_scope_t self);

neo_js_scope_t neo_js_scope_set_parent(neo_js_scope_t self,
                                       neo_js_scope_t parent);

neo_js_handle_t neo_js_scope_get_root_handle(neo_js_scope_t self);

neo_js_variable_t neo_js_scope_get_variable(neo_js_scope_t self,
                                            const wchar_t *name);

void neo_js_scope_set_variable(neo_allocator_t allocator, neo_js_scope_t self,
                               neo_js_variable_t variable, const wchar_t *name);

neo_js_variable_t neo_js_scope_create_variable(neo_allocator_t allocator,
                                               neo_js_scope_t self,
                                               neo_js_handle_t handle);
#ifdef __cplusplus
}
#endif
#endif