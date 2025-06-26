#include "runtime/vm.h"
#include "compiler/asm.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "core/unicode.h"
#include "engine/basetype/function.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>
#include <wchar.h>

typedef struct _neo_js_try_frame_t *neo_js_try_frame_t;

struct _neo_js_try_frame_t {
  neo_js_scope_t scope;
  size_t stack_top;
  size_t onfinish;
  size_t onerror;
};
typedef struct _neo_js_label_frame_t *neo_js_label_frame_t;

struct _neo_js_label_frame_t {
  neo_js_scope_t scope;
  size_t stack_top;
  wchar_t *label;
};

struct _neo_js_vm_t {
  neo_list_t stack;
  neo_list_t try_stack;
  neo_list_t label_stack;
  neo_js_context_t ctx;
  size_t offset;
  neo_js_variable_t self;
};

void neo_js_vm_dispose(neo_allocator_t allocator, neo_js_vm_t vm) {
  neo_allocator_free(allocator, vm->stack);
  neo_allocator_free(allocator, vm->try_stack);
  neo_allocator_free(allocator, vm->label_stack);
}

neo_js_vm_t neo_create_js_vm(neo_js_context_t ctx, neo_js_variable_t self,
                             size_t offset) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_vm_t vm = neo_allocator_alloc(allocator, sizeof(struct _neo_js_vm_t),
                                       neo_js_vm_dispose);
  vm->stack = neo_create_list(allocator, NULL);
  neo_list_initialize_t initialize = {true};
  vm->try_stack = neo_create_list(allocator, &initialize);
  vm->label_stack = neo_create_list(allocator, &initialize);
  vm->ctx = ctx;
  vm->offset = offset;
  vm->self = self ? self : neo_js_context_create_undefined(ctx);
  return vm;
}

neo_asm_code_t neo_js_vm_read_code(neo_js_vm_t vm, neo_program_t program) {
  uint8_t *codes = neo_buffer_get(program->codes);
  codes += vm->offset;
  vm->offset += sizeof(uint16_t);
  return (neo_asm_code_t)(*(uint16_t *)codes);
}

size_t neo_js_vm_read_address(neo_js_vm_t vm, neo_program_t program) {
  uint8_t *codes = neo_buffer_get(program->codes);
  codes += vm->offset;
  vm->offset += sizeof(size_t);
  return *(size_t *)codes;
}

wchar_t *neo_js_vm_read_string(neo_js_vm_t vm, neo_program_t program) {
  uint8_t *codes = neo_buffer_get(program->codes);
  codes += vm->offset;
  vm->offset += sizeof(size_t);
  size_t idx = *(size_t *)codes;
  neo_list_node_t it = neo_list_get_first(program->constants);
  for (size_t i = 0; i < idx; i++) {
    it = neo_list_node_next(it);
  }
  const char *str = neo_list_node_get(it);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  return neo_string_to_wstring(allocator, str);
}

double neo_js_vm_read_number(neo_js_vm_t vm, neo_program_t program) {
  uint8_t *codes = neo_buffer_get(program->codes);
  codes += vm->offset;
  vm->offset += sizeof(double);
  return *(double *)codes;
}

int32_t neo_js_vm_read_integer(neo_js_vm_t vm, neo_program_t program) {
  uint8_t *codes = neo_buffer_get(program->codes);
  codes += vm->offset;
  vm->offset += sizeof(int32_t);
  return *(int32_t *)codes;
}

bool neo_js_vm_read_boolean(neo_js_vm_t vm, neo_program_t program) {
  uint8_t *codes = neo_buffer_get(program->codes);
  codes += vm->offset;
  vm->offset += sizeof(bool);
  return *(bool *)codes;
}

typedef void (*neo_js_vm_cmd_fn_t)(neo_js_vm_t, neo_program_t);

void neo_js_vm_push_scope(neo_js_vm_t vm, neo_program_t program) {
  neo_js_context_push_scope(vm->ctx);
}

void neo_js_vm_pop_scope(neo_js_vm_t vm, neo_program_t program) {
  neo_js_context_pop_scope(vm->ctx);
}

void neo_js_vm_pop(neo_js_vm_t vm, neo_program_t program) {
  neo_list_pop(vm->stack);
}

void neo_js_vm_store(neo_js_vm_t vm, neo_program_t program) {
  wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t current = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t error =
      neo_js_context_store_variable(vm->ctx, current, name);
  neo_allocator_free(neo_js_context_get_allocator(vm->ctx), name);
  if (neo_js_variable_get_type(error)->kind == NEO_TYPE_ERROR) {
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_def(neo_js_vm_t vm, neo_program_t program) {
  wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t current = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t error = neo_js_context_def_variable(vm->ctx, current, name);
  neo_allocator_free(neo_js_context_get_allocator(vm->ctx), name);
  if (neo_js_variable_get_type(error)->kind == NEO_TYPE_ERROR) {
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_load(neo_js_vm_t vm, neo_program_t program) {
  wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t variable = neo_js_context_load_variable(vm->ctx, name);
  neo_list_push(vm->stack, variable);
  neo_allocator_free(neo_js_context_get_allocator(vm->ctx), name);
  if (neo_js_variable_get_type(variable)->kind == NEO_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_clone(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  variable = neo_js_context_clone(vm->ctx, variable);
  neo_list_push(vm->stack, variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_push_undefined(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_undefined(vm->ctx));
}

void neo_js_vm_push_null(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_null(vm->ctx));
}
void neo_js_vm_push_nan(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_number(vm->ctx, NAN));
}
void neo_js_vm_push_infinity(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_number(vm->ctx, INFINITY));
}
void neo_js_vm_push_uninitialize(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_uninitialize(vm->ctx));
}
void neo_js_vm_push_true(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_boolean(vm->ctx, true));
}
void neo_js_vm_push_false(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_boolean(vm->ctx, false));
}
void neo_js_vm_push_number(neo_js_vm_t vm, neo_program_t program) {
  double value = neo_js_vm_read_number(vm, program);
  neo_list_push(vm->stack, neo_js_context_create_number(vm->ctx, value));
}
void neo_js_vm_push_string(neo_js_vm_t vm, neo_program_t program) {
  wchar_t *value = neo_js_vm_read_string(vm, program);
  neo_list_push(vm->stack, neo_js_context_create_string(vm->ctx, value));
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_allocator_free(allocator, value);
}
void neo_js_vm_push_function(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_function(vm->ctx, program));
}
void neo_js_vm_push_object(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_object(vm->ctx, NULL, NULL));
}
void neo_js_vm_push_array(neo_js_vm_t vm, neo_program_t program) {
  double length = neo_js_vm_read_number(vm, program);
  neo_list_push(vm->stack, neo_js_context_create_array(vm->ctx, length));
}
void neo_js_vm_push_value(neo_js_vm_t vm, neo_program_t program) {
  int32_t offset = neo_js_vm_read_integer(vm, program);
  neo_list_node_t it = neo_list_get_tail(vm->stack);
  for (int32_t idx = 0; idx < offset; idx++) {
    it = neo_list_node_last(it);
  }
  neo_js_variable_t variable = neo_list_node_get(it);
  neo_list_push(vm->stack, neo_js_context_create_variable(
                               vm->ctx, neo_js_variable_get_handle(variable)));
}
void neo_js_vm_set_const(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_set_const(variable, true);
}

void neo_js_set_generator(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_TYPE_FUNCTION) {
    neo_list_push(vm->stack,
                  neo_js_context_create_error(vm->ctx, L"TypeError",
                                              L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  function->is_generator = true;
}
void neo_js_set_address(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_TYPE_FUNCTION) {
    neo_list_push(vm->stack,
                  neo_js_context_create_error(vm->ctx, L"TypeError",
                                              L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  function->address = address;
}

void neo_js_set_name(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t name = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_TYPE_FUNCTION) {
    neo_list_push(vm->stack,
                  neo_js_context_create_error(vm->ctx, L"TypeError",
                                              L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  neo_js_handle_t hname = neo_js_variable_get_handle(name);
  neo_js_handle_t hfunction = neo_js_variable_get_handle(variable);
  function->callable.name = hname;
  neo_js_handle_add_parent(hname, hfunction);
}

void neo_js_set_closure(neo_js_vm_t vm, neo_program_t program) {
  wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_TYPE_FUNCTION) {
    neo_list_push(vm->stack,
                  neo_js_context_create_error(vm->ctx, L"TypeError",
                                              L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_variable_t value = neo_js_context_load_variable(vm->ctx, name);
  if (neo_js_variable_get_type(value)->kind == NEO_TYPE_ERROR) {
    neo_list_push(vm->stack, value);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
  neo_js_handle_t hfunction = neo_js_variable_get_handle(variable);
  neo_hash_map_set(function->callable.closure, name, hvalue, NULL, NULL);
  neo_js_handle_add_parent(hvalue, hfunction);
}

void neo_js_set_source(neo_js_vm_t vm, neo_program_t program) {
  wchar_t *source = neo_js_vm_read_string(vm, program);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_TYPE_FUNCTION) {
    neo_list_push(vm->stack,
                  neo_js_context_create_error(vm->ctx, L"TypeError",
                                              L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  function->source = source;
}

void neo_js_set_async(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_TYPE_FUNCTION) {
    neo_list_push(vm->stack,
                  neo_js_context_create_error(vm->ctx, L"TypeError",
                                              L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  function->is_async = true;
}

void neo_js_vm_direcitve(neo_js_vm_t vm, neo_program_t program) {}

void neo_js_vm_call(neo_js_vm_t vm, neo_program_t program) {
  int32_t argc = neo_js_vm_read_integer(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[argc - idx - 1] = neo_list_node_get(neo_list_get_last(vm->stack));
    neo_list_pop(vm->stack);
  }
  neo_js_variable_t callee = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_call(
      vm->ctx, callee, neo_js_context_create_undefined(vm->ctx), argc, argv);
  neo_allocator_free(allocator, argv);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_member_call(neo_js_vm_t vm, neo_program_t program) {
  int32_t argc = neo_js_vm_read_integer(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[argc - idx - 1] = neo_list_node_get(neo_list_get_last(vm->stack));
    neo_list_pop(vm->stack);
  }
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t callee = neo_js_context_get_field(vm->ctx, host, field);
  neo_js_variable_t result =
      neo_js_context_call(vm->ctx, callee, host, argc, argv);
  neo_allocator_free(allocator, argv);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_get_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t value = neo_js_context_get_field(vm->ctx, host, field);
  neo_list_push(vm->stack, value);
  if (neo_js_variable_get_type(value)->kind == NEO_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_set_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t error =
      neo_js_context_set_field(vm->ctx, host, field, value);
  if (neo_js_variable_get_type(error)->kind == NEO_TYPE_ERROR) {
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_set_method(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t error =
      neo_js_context_set_field(vm->ctx, host, field, value);
  if (neo_js_variable_get_type(error)->kind == NEO_TYPE_ERROR) {
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_jnull(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(value)->kind == NEO_TYPE_UNDEFINED ||
      neo_js_variable_get_type(value)->kind == NEO_TYPE_NULL) {
    vm->offset = address;
  }
}

void neo_js_vm_jnot_null(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(value)->kind != NEO_TYPE_UNDEFINED &&
      neo_js_variable_get_type(value)->kind != NEO_TYPE_NULL) {
    vm->offset = address;
  }
}
void neo_js_vm_jfalse(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  value = neo_js_context_to_boolean(vm->ctx, value);
  neo_js_boolean_t boolean = neo_js_variable_to_boolean(value);
  if (!boolean->boolean) {
    vm->offset = address;
  }
}
void neo_js_vm_jtrue(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  value = neo_js_context_to_boolean(vm->ctx, value);
  neo_js_boolean_t boolean = neo_js_variable_to_boolean(value);
  if (boolean->boolean) {
    vm->offset = address;
  }
}
void neo_js_vm_jmp(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  vm->offset = address;
}
void neo_js_vm_ret(neo_js_vm_t vm, neo_program_t program) {
  vm->offset = neo_buffer_get_size(program->codes);
}
void neo_js_vm_hlt(neo_js_vm_t vm, neo_program_t program) {
  if (!neo_list_get_size(vm->stack)) {
    neo_list_push(vm->stack, neo_js_context_create_undefined(vm->ctx));
  }
  vm->offset = neo_buffer_get_size(program->codes);
}

const neo_js_vm_cmd_fn_t cmds[] = {
    neo_js_vm_push_scope,        // NEO_ASM_PUSH_SCOPE
    neo_js_vm_pop_scope,         // NEO_ASM_POP_SCOPE
    neo_js_vm_pop,               // NEO_ASM_POP
    neo_js_vm_store,             // NEO_ASM_STORE
    neo_js_vm_def,               // NEO_ASM_DEF
    neo_js_vm_load,              // NEO_ASM_LOAD
    neo_js_vm_clone,             // NEO_ASM_CLONE
    NULL,                        // NEO_ASM_WITH
    NULL,                        // NEO_ASM_INIT_ACCESSOR
    NULL,                        // NEO_ASM_INIT_FIELD
    neo_js_vm_push_undefined,    // NEO_ASM_PUSH_UNDEFINED
    neo_js_vm_push_null,         // NEO_ASM_PUSH_NULL
    neo_js_vm_push_nan,          // NEO_ASM_PUSH_NAN
    neo_js_vm_push_infinity,     // NEO_ASM_PUSH_INFINTY
    neo_js_vm_push_uninitialize, // NEO_ASM_PUSH_UNINITIALIZED
    neo_js_vm_push_true,         // NEO_ASM_PUSH_TRUE
    neo_js_vm_push_false,        // NEO_ASM_PUSH_FALSE
    neo_js_vm_push_number,       // NEO_ASM_PUSH_NUMBER
    neo_js_vm_push_string,       // NEO_ASM_PUSH_STRING
    NULL,                        // NEO_ASM_PUSH_BIGINT
    NULL,                        // NEO_ASM_PUSH_REGEX
    neo_js_vm_push_function,     // NEO_ASM_PUSH_FUNCTION
    neo_js_vm_push_object,       // NEO_ASM_PUSH_OBJECT
    neo_js_vm_push_array,        // NEO_ASM_PUSH_ARRAY
    NULL,                        // NEO_ASM_PUSH_THIS
    NULL,                        // NEO_ASM_PUSH_SUPER
    neo_js_vm_push_value,        // NEO_ASM_PUSH_VALUE
    NULL,                        // NEO_ASM_PUSH_LABEL
    NULL,                        // NEO_ASM_POP_LABEL
    neo_js_set_async,            // NEO_ASM_SET_ASYNC
    neo_js_vm_set_const,         // NEO_ASM_SET_CONST
    NULL,                        // NEO_ASM_SET_USING
    NULL,                        // NEO_ASM_SET_LAMBDA
    neo_js_set_generator,        // NEO_ASM_SET_GENERATOR
    neo_js_set_source,           // NEO_ASM_SET_SOURCE
    neo_js_set_address,          // NEO_ASM_SET_ADDRESS
    neo_js_set_name,             // NEO_ASM_SET_NAME
    neo_js_set_closure,          // NEO_ASM_SET_CLOSURE
    NULL,                        // NEO_ASM_EXTEND
    NULL,                        // NEO_ASM_DECORATOR
    neo_js_vm_direcitve,         // NEO_ASM_DIRECTIVE
    neo_js_vm_call,              // NEO_ASM_CALL
    neo_js_vm_member_call,       // NEO_ASM_MEMBER_CALL
    neo_js_vm_get_field,         // NEO_ASM_GET_FIELD
    neo_js_vm_set_field,         // NEO_ASM_SET_FIELD
    NULL,                        // NEO_ASM_SET_GETTER
    NULL,                        // NEO_ASM_SET_SETTER
    neo_js_vm_set_method,        // NEO_ASM_SET_METHOD
    neo_js_vm_jnull,             // NEO_ASM_JNULL
    neo_js_vm_jnot_null,         // NEO_ASM_JNOT_NULL
    neo_js_vm_jfalse,            // NEO_ASM_JFALSE
    neo_js_vm_jtrue,             // NEO_ASM_JTRUE
    neo_js_vm_jmp,               // NEO_ASM_JMP
    NULL,                        // NEO_ASM_BREAK
    NULL,                        // NEO_ASM_CONTINUE
    NULL,                        // NEO_ASM_THROW
    NULL,                        // NEO_ASM_DEFER
    NULL,                        // NEO_ASM_TRY
    neo_js_vm_ret,               // NEO_ASM_RET
    neo_js_vm_hlt,               // NEO_ASM_HLT
    NULL,                        // NEO_ASM_KEYS
    NULL,                        // NEO_ASM_AWAIT
    NULL,                        // NEO_ASM_YIELD
    NULL,                        // NEO_ASM_YIELD_DEGELATE
    NULL,                        // NEO_ASM_NEXT
    NULL,                        // NEO_ASM_ITERATOR
    NULL,                        // NEO_ASM_ENTRIES
    NULL,                        // NEO_ASM_REST
    NULL,                        // NEO_ASM_IMPORT
    NULL,                        // NEO_ASM_ASSERT
    NULL,                        // NEO_ASM_EXPORT
    NULL,                        // NEO_ASM_EXPORT_ALL
    NULL,                        // NEO_ASM_BREAKPOINT
    NULL,                        // NEO_ASM_NEW
    NULL,                        // NEO_ASM_EQ
    NULL,                        // NEO_ASM_NE
    NULL,                        // NEO_ASM_SEQ
    NULL,                        // NEO_ASM_GT
    NULL,                        // NEO_ASM_LT
    NULL,                        // NEO_ASM_GE
    NULL,                        // NEO_ASM_LE
    NULL,                        // NEO_ASM_SNE
    NULL,                        // NEO_ASM_DEL
    NULL,                        // NEO_ASM_TYPEOF
    NULL,                        // NEO_ASM_VOID
    NULL,                        // NEO_ASM_INC
    NULL,                        // NEO_ASM_DEC
    NULL,                        // NEO_ASM_ADD
    NULL,                        // NEO_ASM_SUB
    NULL,                        // NEO_ASM_MUL
    NULL,                        // NEO_ASM_DIV
    NULL,                        // NEO_ASM_MOD
    NULL,                        // NEO_ASM_POW
    NULL,                        // NEO_ASM_NOT
    NULL,                        // NEO_ASM_AND
    NULL,                        // NEO_ASM_OR
    NULL,                        // NEO_ASM_XOR
    NULL,                        // NEO_ASM_SHR
    NULL,                        // NEO_ASM_SHL
    NULL,                        // NEO_ASM_USHR
    NULL,                        // NEO_ASM_PLUS
    NULL,                        // NEO_ASM_NEG
    NULL,                        // NEO_ASM_LOGICAL_NOT
    NULL,                        // NEO_ASM_CONCAT
    NULL,                        // NEO_ASM_MERGE
    NULL,                        // NEO_ASM_SPREAD
};

neo_js_variable_t neo_js_vm_eval(neo_js_vm_t vm, neo_program_t program) {
  while (true) {
    if (vm->offset == neo_buffer_get_size(program->codes)) {
      break;
    }
    neo_asm_code_t code = neo_js_vm_read_code(vm, program);
    if (cmds[code]) {
      cmds[code](vm, program);
    } else {
      return neo_js_context_create_error(vm->ctx, L"InternalError",
                                         L"Unsupport ASM");
    }
  }
  if (neo_list_get_size(vm->stack)) {
    return neo_list_node_get(neo_list_get_last(vm->stack));
  }
  return neo_js_context_create_undefined(vm->ctx);
}