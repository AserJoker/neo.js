#ifndef _H_NEO_ENGINE_VARIABLE_
#define _H_NEO_ENGINE_VARIABLE_
#include "core/allocator.h"
#include "core/list.h"
#include "engine/value.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_context_t *neo_js_context_t;

struct _neo_js_variable_t {
  bool is_using;
  bool is_await_using;

  int32_t ref;

  neo_js_value_t value;
};

typedef struct _neo_js_variable_t *neo_js_variable_t;
typedef neo_js_variable_t (*neo_js_cfunc_t)(neo_js_context_t ctx,
                                            neo_js_variable_t self, size_t argc,
                                            neo_js_variable_t *argv);

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

void neo_js_variable_gc(neo_allocator_t allocator, neo_list_t variables,
                        neo_list_t gclist);

#ifdef __cplusplus
}
#endif
#endif