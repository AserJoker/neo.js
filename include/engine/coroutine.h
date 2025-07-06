#ifndef _H_NEO_ENGINE_COROUTINE_
#define _H_NEO_ENGINE_COROUTINE_
#include "engine/handle.h"
#include "engine/scope.h"
#include "runtime/vm.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_coroutine_t *neo_js_coroutine_t;

struct _neo_js_coroutine_t {
  neo_js_handle_t value;
  neo_js_scope_t root;
  neo_js_scope_t scope;
  neo_js_vm_t vm;
  neo_program_t program;
  bool running;
};

neo_js_coroutine_t neo_create_js_coroutine(neo_allocator_t allocator);

#ifdef __cplusplus
}
#endif
#endif