#ifndef _H_NEO_ENGINE_SCOPE_
#define _H_NEO_ENGINE_SCOPE_
#include "neo.js/core/allocator.h"
#include "neo.js/engine/value.h"
#include "neo.js/engine/variable.h"


#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_scope_t *neo_js_scope_t;

neo_js_scope_t neo_create_js_scope(neo_allocator_t allocator,
                                   neo_js_scope_t parent);

neo_js_scope_t neo_js_scope_get_parent(neo_js_scope_t self);

neo_js_variable_t neo_js_scope_get_variable(neo_js_scope_t self,
                                            const char *name);

neo_js_variable_t neo_js_scope_set_variable(neo_js_scope_t self,
                                            neo_js_variable_t variable,
                                            const char *name);

void neo_js_scope_set_using(neo_js_scope_t self, neo_js_variable_t variable);

void neo_js_scope_set_await_using(neo_js_scope_t self,
                                  neo_js_variable_t variable);

neo_js_variable_t neo_js_scope_create_variable(neo_js_scope_t self,
                                               neo_js_value_t value,
                                               const char *name);

neo_list_t neo_js_scope_get_variables(neo_js_scope_t self);
neo_list_t neo_js_scope_get_children(neo_js_scope_t self);
neo_list_t neo_js_scope_get_using(neo_js_scope_t self);
neo_list_t neo_js_scope_get_await_using(neo_js_scope_t self);

void neo_js_scope_set_feature(neo_js_scope_t self, const char *feature);

bool neo_js_scope_has_feature(neo_js_scope_t self, const char *feature);

void neo_js_scope_defer_free(neo_js_scope_t self, void *data);
#ifdef __cplusplus
}
#endif
#endif