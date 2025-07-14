#ifndef _H_NEO_RUNTIME_VM_
#define _H_NEO_RUNTIME_VM_
#include "compiler/program.h"
#include "engine/type.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_vm_t *neo_js_vm_t;
typedef struct _neo_js_scope_t *neo_js_scope_t;

struct _neo_js_vm_t {
  neo_list_t stack;
  neo_list_t try_stack;
  neo_list_t label_stack;
  neo_js_context_t ctx;
  size_t offset;
  neo_js_variable_t self;
  neo_js_variable_t clazz;
  neo_js_scope_t root;
  neo_js_scope_t scope;
};

neo_js_vm_t neo_create_js_vm(neo_js_context_t ctx, neo_js_variable_t self,
                             neo_js_variable_t clazz, size_t offset,
                             neo_js_scope_t scope);

neo_js_variable_t neo_js_vm_eval(neo_js_vm_t vm, neo_program_t program);

#ifdef __cplusplus
}
#endif
#endif