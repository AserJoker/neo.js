#ifndef _H_NEO_ENGINE_VARIABLE_
#define _H_NEO_ENGINE_VARIABLE_
#include "core/allocator.h"
#include "core/list.h"
#include "engine/handle.h"
#include "engine/value.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_context_t *neo_js_context_t;

struct _neo_js_variable_t {
  struct _neo_js_handle_t handle;
  bool is_using;
  bool is_await_using;
  bool is_const;
  neo_js_value_t value;
};

typedef struct _neo_js_variable_t *neo_js_variable_t;
typedef neo_js_variable_t (*neo_js_cfunc_t)(neo_js_context_t ctx,
                                            neo_js_variable_t self, size_t argc,
                                            neo_js_variable_t *argv);

#define NEO_JS_CFUNCTION(name)                                                 \
  neo_js_variable_t name(neo_js_context_t ctx, neo_js_variable_t self,         \
                         size_t argc, neo_js_variable_t *argv)
neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_value_t value);
neo_js_variable_t neo_js_variable_to_string(neo_js_variable_t self,
                                            neo_js_context_t ctx);
neo_js_variable_t neo_js_variable_to_object(neo_js_variable_t self,
                                            neo_js_context_t ctx);
neo_js_variable_t neo_js_variable_to_primitive(neo_js_variable_t self,
                                               neo_js_context_t ctx,
                                               const char *hint);
neo_js_variable_t
neo_js_variable_def_field(neo_js_variable_t self, neo_js_context_t ctx,
                          neo_js_variable_t key, neo_js_variable_t value,
                          bool configurable, bool enumable, bool writable);
neo_js_variable_t
neo_js_variable_def_accessor(neo_js_variable_t self, neo_js_context_t ctx,
                             neo_js_variable_t key, neo_js_variable_t get,
                             neo_js_variable_t set, bool configurable,
                             bool enumable, bool writable);
neo_js_variable_t neo_js_variable_get_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key);
neo_js_variable_t neo_js_variable_set_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key,
                                            neo_js_variable_t value);
neo_js_variable_t neo_js_variable_del_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key);
neo_js_variable_t neo_js_variable_call(neo_js_variable_t self,
                                       neo_js_context_t ctx,
                                       neo_js_variable_t bind, size_t argc,
                                       neo_js_variable_t *argv);
neo_js_variable_t neo_js_variable_get_internel(neo_js_variable_t self,
                                               neo_js_context_t ctx,
                                               const char *name);
neo_js_variable_t neo_js_variable_set_internal(neo_js_variable_t self,
                                               neo_js_context_t ctx,
                                               const char *name,
                                               neo_js_variable_t value);
void neo_js_variable_set_opaque(neo_js_variable_t self, neo_js_context_t ctx,
                                const char *name, void *value);
void *neo_js_variable_get_opaque(neo_js_variable_t self, neo_js_context_t ctx,
                                 const char *name);
neo_js_variable_t neo_js_variable_set_prototype_of(neo_js_variable_t self,
                                                   neo_js_context_t ctx,
                                                   neo_js_variable_t prototype);
neo_js_variable_t neo_js_variable_get_prototype_of(neo_js_variable_t self,
                                                   neo_js_context_t ctx);
neo_js_variable_t neo_js_variable_extends(neo_js_variable_t self,
                                          neo_js_context_t ctx,
                                          neo_js_variable_t parent);
neo_js_variable_t neo_js_variable_set_closure(neo_js_variable_t self,
                                              neo_js_context_t ctx,
                                              const uint16_t *name,
                                              neo_js_variable_t value);
void neo_js_variable_gc(neo_allocator_t allocator, neo_list_t variables);

#ifdef __cplusplus
}
#endif
#endif