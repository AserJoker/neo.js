#ifndef _H_NEO_JS_HANDLE_
#define _H_NEO_JS_HANDLE_
#include "core/allocator.h"
#include "core/list.h"
#include "js/value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_handle_t *neo_js_handle_t;

neo_js_handle_t neo_create_js_handle(neo_allocator_t allocator,
                                     neo_js_value_t value);

neo_js_value_t neo_js_handle_get_value(neo_js_handle_t self);

void neo_js_handle_set_value(neo_js_handle_t self, neo_js_value_t value);

void neo_js_handle_add_parent(neo_js_handle_t self, neo_js_handle_t parent);

void neo_js_handle_remove_parent(neo_js_handle_t self, neo_js_handle_t parent);

neo_list_t neo_js_handle_get_parents(neo_js_handle_t self);

neo_list_t neo_js_handle_get_children(neo_js_handle_t self);

#ifdef __cplusplus
}
#endif
#endif