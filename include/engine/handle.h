#ifndef _H_NEO_ENGINE_HANDLE_
#define _H_NEO_ENGINE_HANDLE_
#include "core/allocator.h"
#include "core/list.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_engine_handle_t *neo_engine_handle_t;

neo_engine_handle_t neo_create_js_handle(neo_allocator_t allocator,
                                         neo_engine_value_t value);

neo_engine_value_t neo_engine_handle_get_value(neo_engine_handle_t self);

void neo_engine_handle_set_value(neo_allocator_t allocator,
                                 neo_engine_handle_t self,
                                 neo_engine_value_t value);

void neo_engine_handle_add_parent(neo_engine_handle_t self,
                                  neo_engine_handle_t parent);

void neo_engine_handle_remove_parent(neo_engine_handle_t self,
                                     neo_engine_handle_t parent);

neo_list_t neo_engine_handle_get_parents(neo_engine_handle_t self);

neo_list_t neo_engine_handle_get_children(neo_engine_handle_t self);

#ifdef __cplusplus
}
#endif
#endif