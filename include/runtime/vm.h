#ifndef _H_NEO_RUNTIME_VM_
#define _H_NEO_RUNTIME_VM_
#include "compiler/program.h"
#include "engine/type.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_vm_t *neo_js_vm_t;
neo_js_vm_t neo_create_js_vm(neo_js_context_t ctx, neo_js_variable_t self,
                             size_t offset);

neo_js_variable_t neo_js_vm_eval(neo_js_vm_t vm, neo_program_t program);

neo_list_t neo_js_vm_get_stack(neo_js_vm_t vm);

void neo_js_vm_set_offset(neo_js_vm_t vm, size_t offset);

#ifdef __cplusplus
}
#endif
#endif