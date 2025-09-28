#ifndef _H_NEO_ENGINE_handle_
#define _H_NEO_ENGINE_handle_
#include "core/allocator.h"
#include "core/list.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_handle_t *neo_js_handle_t;

neo_js_handle_t neo_create_js_handle(neo_allocator_t allocator,
                                     neo_js_value_t value);

neo_js_value_t neo_js_handle_get_value(neo_js_handle_t self);

void neo_js_handle_set_value(neo_allocator_t allocator, neo_js_handle_t self,
                             neo_js_value_t value);

void neo_js_handle_add_parent(neo_js_handle_t self, neo_js_handle_t parent);

void neo_js_handle_add_weak_parent(neo_js_handle_t self,
                                   neo_js_handle_t parent);

void neo_js_handle_remove_parent(neo_js_handle_t self, neo_js_handle_t parent);

void neo_js_handle_remove_weak_parent(neo_js_handle_t self,
                                      neo_js_handle_t parent);

neo_list_t neo_js_handle_get_parents(neo_js_handle_t self);

neo_list_t neo_js_handle_get_children(neo_js_handle_t self);

neo_list_t neo_js_handle_get_week_children(neo_js_handle_t self);

neo_list_t neo_js_handle_get_week_parent(neo_js_handle_t self);

bool neo_js_handle_check_alive(neo_allocator_t allocator,
                               neo_js_handle_t handle);

void neo_js_handle_gc(neo_allocator_t allocator, neo_js_handle_t root);

#ifdef __cplusplus
}
#endif
#endif