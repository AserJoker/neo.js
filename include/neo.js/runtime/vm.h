#ifndef _H_NEO_RUNTIME_VM_
#define _H_NEO_RUNTIME_VM_
#include "neo.js/compiler/program.h"
#include "neo.js/engine/variable.h"


#ifdef __cplusplus
extern "C" {
#endif

struct _neo_js_vm_t {
  neo_list_t stack;
  neo_list_t trystack;
  neo_list_t labelstack;
  neo_js_variable_t result;
  neo_js_variable_t self;
  neo_js_variable_t clazz;
};
typedef struct _neo_js_vm_t *neo_js_vm_t;
typedef void (*neo_js_vm_handle_fn_t)(neo_js_vm_t vm, neo_js_context_t ctx,
                                      neo_js_program_t program, size_t *offset);
neo_js_vm_t neo_create_js_vm(neo_js_context_t ctx, neo_js_variable_t self);
neo_js_variable_t neo_js_vm_run(neo_js_vm_t self, neo_js_context_t ctx,
                                neo_js_program_t program, size_t offset);
#ifdef __cplusplus
}
#endif
#endif