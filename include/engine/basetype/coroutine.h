#ifndef _H_NEO_ENGINE_BASETYPE_COROUTINE_
#define _H_NEO_ENGINE_BASETYPE_COROUTINE_
#include "core/allocator.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/value.h"
#include "runtime/vm.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_coroutine_t *neo_js_coroutine_t;

typedef struct _neo_js_scope_t *neo_js_scope_t;

typedef struct _neo_js_co_context_t *neo_js_co_context_t;

struct _neo_js_co_context_t {
  neo_js_handle_t result;
  neo_js_scope_t root;
  neo_js_scope_t scope;
  neo_js_vm_t vm;
  neo_program_t program;
  bool running;
};

struct _neo_js_coroutine_t {
  struct _neo_js_value_t value;
  neo_js_co_context_t ctx;
};

neo_js_type_t neo_get_js_coroutine_type();
neo_js_co_context_t neo_create_js_co_context(neo_allocator_t allocator,
                                             neo_js_vm_t vm,
                                             neo_program_t program,
                                             neo_js_scope_t root);

neo_js_coroutine_t neo_create_js_coroutine(neo_allocator_t allocator,
                                           neo_js_co_context_t ctx);

neo_js_coroutine_t neo_js_value_to_coroutine(neo_js_value_t variable);

neo_js_co_context_t neo_js_coroutine_get_context(neo_js_variable_t coroutine);


#ifdef __cplusplus
}
#endif
#endif