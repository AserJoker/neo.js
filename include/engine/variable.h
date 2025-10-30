#ifndef _H_NEO_ENGINE_VARIABLE_
#define _H_NEO_ENGINE_VARIABLE_
#include "core/allocator.h"
#include "core/list.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_context_t *neo_js_context_t;
typedef enum _neo_js_variable_type_t {
  NEO_JS_TYPE_INTERRUPT,
  NEO_JS_TYPE_EXCEPTION,
  NEO_JS_TYPE_UNDEFINED,
  NEO_JS_TYPE_NULL,
  NEO_JS_TYPE_NUMBER,
  NEO_JS_TYPE_BOOLEAN,
  NEO_JS_TYPE_STRING,
  NEO_JS_TYPE_SYMBOL,
  NEO_JS_TYPE_OBJECT,
  NEO_JS_TYPE_ARRAY,
  NEO_JS_TYPE_FUNCTION
} neo_js_variable_type_t;

struct _neo_js_value_t {
  uint32_t ref;
  neo_js_variable_type_t type;
};

typedef struct _neo_js_value_t *neo_js_value_t;

struct _neo_js_variable_t {
  neo_allocator_t allocator;
  bool is_;
  bool is_using;
  bool is_await_using;

  int32_t ref;
  neo_list_t parents;
  neo_list_t children;
  neo_list_t weak_parents;
  neo_list_t weak_children;

  bool is_check;
  bool is_alive;
  bool is_disposed;
  uint32_t age;

  neo_js_value_t value;
};

typedef struct _neo_js_variable_t *neo_js_variable_t;
typedef neo_js_variable_t (*neo_js_cfunc_t)(neo_js_context_t ctx,
                                            neo_js_variable_t self, size_t argc,
                                            neo_js_variable_t *argv);
neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_value_t value);
void neo_js_variable_add_parent(neo_js_variable_t self,
                                neo_js_variable_t parent);
void neo_js_variable_remove_parent(neo_js_variable_t self,
                                   neo_js_variable_t parent);
void neo_js_variable_add_weak_parent(neo_js_variable_t self,
                                     neo_js_variable_t parent);
void neo_js_variable_remove_weak_parent(neo_js_variable_t self,
                                        neo_js_variable_t parent);
void neo_js_variable_gc(neo_allocator_t allocator, neo_list_t gclist);

void neo_init_js_value(neo_js_value_t self, neo_allocator_t allocator,
                       neo_js_variable_type_t type);
void neo_deinit_js_value(neo_js_value_t self, neo_allocator_t allocator);

neo_js_variable_t neo_js_variable_to_string(neo_js_variable_t self,
                                            neo_js_context_t ctx);

neo_js_variable_t neo_js_variable_to_number(neo_js_variable_t self,
                                            neo_js_context_t ctx);

neo_js_variable_t neo_js_variable_to_boolean(neo_js_variable_t self,
                                             neo_js_context_t ctx);

neo_js_variable_t neo_js_variable_to_object(neo_js_variable_t self,
                                            neo_js_context_t ctx);

neo_js_variable_t
neo_js_variable_def_field(neo_js_variable_t self, neo_js_context_t ctx,
                          neo_js_variable_t key, neo_js_variable_t value,
                          bool configurable, bool enumable, bool writable);

neo_js_variable_t
neo_js_variable_def_accessor(neo_js_variable_t self, neo_js_context_t ctx,
                             neo_js_variable_t key, neo_js_variable_t get,
                             neo_js_variable_t set, bool configurable,
                             bool enumable);

neo_js_variable_t neo_js_variable_get_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key);

neo_js_variable_t neo_js_variable_set_field(neo_js_variable_t self,
                                            neo_js_context_t ctx,
                                            neo_js_variable_t key,
                                            neo_js_variable_t value);

neo_js_variable_t neo_js_variable_call(neo_js_variable_t self,
                                       neo_js_context_t ctx,
                                       neo_js_variable_t _this, uint32_t argc,
                                       neo_js_variable_t *argv);

neo_js_variable_t neo_js_variable_equal(neo_js_variable_t self,
                                        neo_js_context_t ctx,
                                        neo_js_variable_t another);

#ifdef __cplusplus
}
#endif
#endif