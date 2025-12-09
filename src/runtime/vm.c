#include "runtime/vm.h"
#include "compiler/asm.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "core/string.h"
#include "engine/boolean.h"
#include "engine/callable.h"
#include "engine/context.h"
#include "engine/exception.h"
#include "engine/function.h"
#include "engine/handle.h"
#include "engine/interrupt.h"
#include "engine/number.h"
#include "engine/object.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/signal.h"
#include "engine/string.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/array.h"
#include "runtime/constant.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define NEO_JS_VM_CHECK(vm, expression, program, offset)                       \
  do {                                                                         \
    neo_js_variable_t __error = (expression);                                  \
    if (__error->value->type == NEO_JS_TYPE_EXCEPTION) {                       \
      vm->result = __error;                                                    \
      *offset = neo_buffer_get_size(program->codes);                           \
      return;                                                                  \
    }                                                                          \
  } while (0)

struct _neo_js_try_frame_t {
  size_t onfinish;
  size_t onerror;
  neo_js_scope_t scope;
  size_t stacktop;
  size_t labelstack_top;
  neo_js_variable_t result;
};

typedef struct _neo_js_try_frame_t *neo_js_try_frame_t;

enum _neo_js_label_type_t { NEO_JS_LABEL_BREAK, NEO_JS_LABEL_CONTINUE };
typedef enum _neo_js_label_type_t neo_js_label_type_t;

struct _neo_js_label_frame_t {
  const char *name;
  size_t trystack_top;
  size_t stacktop;
  size_t address;
  neo_js_scope_t scope;
  neo_js_label_type_t type;
};
typedef struct _neo_js_label_frame_t *neo_js_label_frame_t;

struct _neo_js_jmp_signal_t {
  const char *name;
  size_t address;
};

typedef struct _neo_js_jmp_signal_t *neo_js_jmp_signal_t;

static void neo_js_vm_dispose(neo_allocator_t allocator, neo_js_vm_t self) {
  neo_allocator_free(allocator, self->labelstack);
  neo_allocator_free(allocator, self->trystack);
  neo_allocator_free(allocator, self->stack);
}

neo_js_vm_t neo_create_js_vm(neo_js_context_t ctx, neo_js_variable_t self) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_vm_t vm = neo_allocator_alloc(allocator, sizeof(struct _neo_js_vm_t),
                                       neo_js_vm_dispose);
  vm->stack = neo_create_list(allocator, NULL);
  vm->result = neo_js_context_get_undefined(ctx);
  if (!self) {
    vm->self = neo_js_context_get_undefined(ctx);
  } else {
    vm->self = self;
  }
  neo_list_initialize_t initialize = {true};
  vm->trystack = neo_create_list(allocator, &initialize);
  vm->labelstack = neo_create_list(allocator, &initialize);
  return vm;
}
static neo_asm_code_t neo_js_vm_read_code(neo_program_t program,
                                          size_t *offset) {
  uint8_t *codes = neo_buffer_get(program->codes);
  neo_asm_code_t code = (neo_asm_code_t) * (uint16_t *)&codes[*offset];
  *offset += sizeof(uint16_t);
  return code;
}

static size_t neo_js_vm_read_address(neo_program_t program, size_t *offset) {
  uint8_t *codes = neo_buffer_get(program->codes);
  size_t code = (size_t)*(uint16_t *)&codes[*offset];
  *offset += sizeof(size_t);
  return code;
}

static int32_t neo_js_vm_read_integer(neo_program_t program, size_t *offset) {
  uint8_t *codes = neo_buffer_get(program->codes);
  int32_t code = (int32_t)*(uint16_t *)&codes[*offset];
  *offset += sizeof(int32_t);
  return code;
}

static const char *neo_js_vm_read_string(neo_program_t program,
                                         size_t *offset) {
  uint8_t *codes = neo_buffer_get(program->codes);
  size_t idx = *(size_t *)&codes[*offset];
  *offset += sizeof(size_t);
  neo_list_node_t it = neo_list_get_first(program->constants);
  for (size_t i = 0; i < idx; i++) {
    it = neo_list_node_next(it);
  }
  return neo_list_node_get(it);
}

static double neo_js_vm_read_number(neo_program_t program, size_t *offset) {
  uint8_t *codes = neo_buffer_get(program->codes);
  double number = *(double *)&codes[*offset];
  *offset += sizeof(double);
  return number;
}

static neo_js_variable_t neo_js_vm_get_value(neo_js_vm_t vm) {
  return neo_list_node_get(neo_list_get_last(vm->stack));
}

static void neo_js_vm_push_scope(neo_js_vm_t vm, neo_js_context_t ctx,
                                 neo_program_t program, size_t *offset) {
  neo_js_context_push_scope(ctx);
}

static void neo_js_vm_pop_scope(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  if (vm->result) {
    neo_js_scope_t scope = neo_js_context_get_scope(ctx);
    neo_js_scope_t parent = neo_js_scope_get_parent(scope);
    neo_js_scope_set_variable(parent, vm->result, NULL);
  }
  neo_js_context_pop_scope(ctx);
}

static void neo_js_vm_pop(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_list_pop(vm->stack);
}

static void neo_js_vm_store(neo_js_vm_t vm, neo_js_context_t ctx,
                            neo_program_t program, size_t *offset) {
  const char *name = neo_js_vm_read_string(program, offset);
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_js_variable_t res = neo_js_context_store(ctx, name, value);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}
static void neo_js_vm_save(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  vm->result = value;
}
static void neo_js_vm_def(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  const char *name = neo_js_vm_read_string(program, offset);
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_js_variable_t res = neo_js_context_def(ctx, name, value);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}

static void neo_js_vm_load(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  const char *name = neo_js_vm_read_string(program, offset);
  neo_js_variable_t value = neo_js_context_load(ctx, name);
  NEO_JS_VM_CHECK(vm, value, program, offset);
  neo_list_push(vm->stack, value);
}

static void neo_js_vm_push_undefined(neo_js_vm_t vm, neo_js_context_t ctx,
                                     neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_context_get_undefined(ctx);
  neo_list_push(vm->stack, value);
}
static void neo_js_vm_push_null(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_context_get_null(ctx);
  neo_list_push(vm->stack, value);
}
static void neo_js_vm_push_nan(neo_js_vm_t vm, neo_js_context_t ctx,
                               neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_context_create_number(ctx, NAN);
  neo_list_push(vm->stack, value);
}
static void neo_js_vm_push_infinity(neo_js_vm_t vm, neo_js_context_t ctx,
                                    neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_context_create_number(ctx, INFINITY);
  neo_list_push(vm->stack, value);
}

static void neo_js_vm_push_uninitialized(neo_js_vm_t vm, neo_js_context_t ctx,
                                         neo_program_t program,
                                         size_t *offset) {
  neo_js_variable_t uninitialized = neo_js_context_get_uninitialized(ctx);
  neo_list_push(vm->stack, uninitialized);
}

static void neo_js_vm_push_true(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_context_get_true(ctx);
  neo_list_push(vm->stack, value);
}

static void neo_js_vm_push_false(neo_js_vm_t vm, neo_js_context_t ctx,
                                 neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_context_get_false(ctx);
  neo_list_push(vm->stack, value);
}

static void neo_js_vm_push_number(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  double number = neo_js_vm_read_number(program, offset);
  neo_js_variable_t value = neo_js_context_create_number(ctx, number);
  neo_list_push(vm->stack, value);
}
static void neo_js_vm_push_string(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  const char *string = neo_js_vm_read_string(program, offset);
  neo_js_variable_t value = neo_js_context_create_cstring(ctx, string);
  neo_list_push(vm->stack, value);
}
static void neo_js_vm_push_bigint(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  const char *string = neo_js_vm_read_string(program, offset);
  neo_js_variable_t value = neo_js_context_create_cbigint(ctx, string);
  NEO_JS_VM_CHECK(vm, value, program, offset);
  neo_list_push(vm->stack, value);
}

static void neo_js_vm_push_function(neo_js_vm_t vm, neo_js_context_t ctx,
                                    neo_program_t program, size_t *offset) {
  neo_js_variable_t function = neo_js_context_create_function(ctx, program);
  neo_list_push(vm->stack, function);
}
static void neo_js_vm_push_async_function(neo_js_vm_t vm, neo_js_context_t ctx,
                                          neo_program_t program,
                                          size_t *offset) {
  neo_js_variable_t function =
      neo_js_context_create_async_function(ctx, program);
  neo_list_push(vm->stack, function);
}
static void neo_js_vm_push_lambda(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  neo_js_variable_t function = neo_js_context_create_function(ctx, program);
  neo_js_callable_t callable = (neo_js_callable_t)function->value;
  callable->lambda = true;
  neo_js_variable_set_bind(function, ctx, vm->self);
  neo_list_push(vm->stack, function);
}
static void neo_js_vm_push_async_lambda(neo_js_vm_t vm, neo_js_context_t ctx,
                                        neo_program_t program, size_t *offset) {
  neo_js_variable_t function =
      neo_js_context_create_async_function(ctx, program);
  neo_js_callable_t callable = (neo_js_callable_t)function->value;
  callable->lambda = true;
  neo_js_variable_set_bind(function, ctx, vm->self);
  neo_list_push(vm->stack, function);
}
static void neo_js_vm_push_generator(neo_js_vm_t vm, neo_js_context_t ctx,
                                     neo_program_t program, size_t *offset) {
  neo_js_variable_t function =
      neo_js_context_create_generator_function(ctx, program);
  neo_list_push(vm->stack, function);
}
static void neo_js_vm_push_async_generator(neo_js_vm_t vm, neo_js_context_t ctx,
                                           neo_program_t program,
                                           size_t *offset) {
  neo_js_variable_t function =
      neo_js_context_create_async_generator_function(ctx, program);
  neo_list_push(vm->stack, function);
}
static void neo_js_vm_push_object(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  neo_js_variable_t object = neo_js_context_create_object(ctx, NULL);
  neo_list_push(vm->stack, object);
}

static void neo_js_vm_push_array(neo_js_vm_t vm, neo_js_context_t ctx,
                                 neo_program_t program, size_t *offset) {
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  neo_list_push(vm->stack, array);
}

static void neo_js_vm_push_this(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_list_push(vm->stack, vm->self);
}

static void neo_js_vm_member_call(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  int32_t line = neo_js_vm_read_integer(program, offset);
  int32_t column = neo_js_vm_read_integer(program, offset);
  neo_js_variable_t argument = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t key = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t length = neo_js_variable_get_field(
      argument, ctx, neo_js_context_create_cstring(ctx, "length"));
  uint32_t argc = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t argv[argc];
  for (uint32_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_variable_get_field(
        argument, ctx, neo_js_context_create_number(ctx, idx));
  }
  neo_js_variable_t callable = neo_js_variable_get_field(host, ctx, key);
  NEO_JS_VM_CHECK(vm, callable, program, offset);
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_variable_t name = neo_js_variable_get_field(
      callable, ctx, neo_js_context_create_cstring(ctx, "name"));
  NEO_JS_VM_CHECK(vm, name, program, offset);
  const uint16_t *funcname = ((neo_js_string_t)name->value)->value;
  neo_js_context_push_callstack(ctx, program->filename, funcname, line, column);
  neo_js_variable_t result =
      neo_js_variable_call(callable, ctx, host, argc, argv);
  neo_js_context_pop_callstack(ctx);
  NEO_JS_VM_CHECK(vm, result, program, offset);
  neo_list_push(vm->stack, result);
}

static void neo_js_vm_push_value(neo_js_vm_t vm, neo_js_context_t ctx,
                                 neo_program_t program, size_t *offset) {
  int32_t idx = neo_js_vm_read_integer(program, offset);
  neo_list_node_t it = neo_list_get_tail(vm->stack);
  while (idx > 0) {
    it = neo_list_node_last(it);
    idx--;
  }
  neo_js_variable_t value = neo_list_node_get(it);
  neo_list_push(vm->stack, value);
}
static void neo_js_vm_push_break_label(neo_js_vm_t vm, neo_js_context_t ctx,
                                       neo_program_t program, size_t *offset) {
  const char *label = neo_js_vm_read_string(program, offset);
  size_t address = neo_js_vm_read_address(program, offset);
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_label_frame_t frame = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_label_frame_t), NULL);
  frame->name = label;
  frame->scope = neo_js_context_get_scope(ctx);
  frame->stacktop = neo_list_get_size(vm->stack);
  frame->trystack_top = neo_list_get_size(vm->trystack);
  frame->address = address;
  frame->type = NEO_JS_LABEL_BREAK;
  neo_list_push(vm->labelstack, frame);
}
static void neo_js_vm_push_continue_label(neo_js_vm_t vm, neo_js_context_t ctx,
                                          neo_program_t program,
                                          size_t *offset) {
  const char *label = neo_js_vm_read_string(program, offset);
  size_t address = neo_js_vm_read_address(program, offset);
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_label_frame_t frame = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_label_frame_t), NULL);
  frame->name = label;
  frame->scope = neo_js_context_get_scope(ctx);
  frame->stacktop = neo_list_get_size(vm->stack);
  frame->trystack_top = neo_list_get_size(vm->trystack);
  frame->address = address;
  frame->type = NEO_JS_LABEL_CONTINUE;
  neo_list_push(vm->labelstack, frame);
}
static void neo_js_vm_pop_label(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_list_pop(vm->labelstack);
}

static void neo_js_vm_set_const(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  value->is_const = true;
}
static void neo_js_vm_set_using(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  value->is_using = true;
}
static void neo_js_vm_set_await_using(neo_js_vm_t vm, neo_js_context_t ctx,
                                      neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  value->is_await_using = true;
}
static void neo_js_vm_set_source(neo_js_vm_t vm, neo_js_context_t ctx,
                                 neo_program_t program, size_t *offset) {
  neo_js_variable_t function = neo_js_vm_get_value(vm);
  const char *source = neo_js_vm_read_string(program, offset);
  neo_js_function_t func = (neo_js_function_t)function->value;
  func->source = source;
}

static void neo_js_vm_set_address(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  neo_js_variable_t function = neo_js_vm_get_value(vm);
  size_t address = neo_js_vm_read_address(program, offset);
  neo_js_function_t func = (neo_js_function_t)function->value;
  func->address = address;
}

static void neo_js_vm_set_name(neo_js_vm_t vm, neo_js_context_t ctx,
                               neo_program_t program, size_t *offset) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t func = neo_js_vm_get_value(vm);
  const char *name = neo_js_vm_read_string(program, offset);
  neo_js_variable_t funcname = neo_js_context_create_cstring(ctx, name);
  neo_js_variable_def_field(func, ctx, constant->key_name, funcname, false,
                            false, false);
}

static void neo_js_vm_set_closure(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  neo_js_variable_t func = neo_js_vm_get_value(vm);
  const char *name = neo_js_vm_read_string(program, offset);
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_js_variable_t result = neo_js_context_load(ctx, name);
  NEO_JS_VM_CHECK(vm, result, program, offset);
  result = neo_js_variable_set_closure(func, ctx, name, result);
  NEO_JS_VM_CHECK(vm, result, program, offset);
}

static void neo_js_vm_directive(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  const char *feature = neo_js_vm_read_string(program, offset);
  // TODO: directive
}

static void neo_js_vm_call(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  int32_t line = neo_js_vm_read_integer(program, offset);
  int32_t column = neo_js_vm_read_integer(program, offset);
  neo_js_variable_t argument = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t callable = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t length = neo_js_variable_get_field(
      argument, ctx, neo_js_context_create_cstring(ctx, "length"));
  uint32_t argc = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t argv[argc];
  for (uint32_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_variable_get_field(
        argument, ctx, neo_js_context_create_number(ctx, idx));
  }
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_variable_t name = neo_js_variable_get_field(
      callable, ctx, neo_js_context_create_cstring(ctx, "name"));
  const uint16_t *funcname = ((neo_js_string_t)name->value)->value;
  neo_js_context_push_callstack(ctx, program->filename, funcname, line, column);
  neo_js_variable_t result = neo_js_variable_call(
      callable, ctx, neo_js_context_get_undefined(ctx), argc, argv);
  neo_js_context_pop_callstack(ctx);
  NEO_JS_VM_CHECK(vm, result, program, offset);
  neo_list_push(vm->stack, result);
}

static void neo_js_vm_push_back(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t argument = neo_js_vm_get_value(vm);
  neo_js_variable_t res = neo_js_array_push(ctx, argument, 1, &value);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}

static void neo_js_get_field(neo_js_vm_t vm, neo_js_context_t ctx,
                             neo_program_t program, size_t *offset) {
  neo_js_variable_t key = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t object = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t value = neo_js_variable_get_field(object, ctx, key);
  NEO_JS_VM_CHECK(vm, value, program, offset);
  neo_list_push(vm->stack, value);
}

static void neo_js_set_field(neo_js_vm_t vm, neo_js_context_t ctx,
                             neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t key = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t object = neo_js_vm_get_value(vm);
  neo_js_variable_t res = neo_js_variable_set_field(object, ctx, key, value);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}
static void neo_js_set_setter(neo_js_vm_t vm, neo_js_context_t ctx,
                              neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t key = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t object = neo_js_vm_get_value(vm);
  neo_js_object_property_t prop =
      neo_js_variable_get_own_property(object, ctx, key);
  if (!prop) {
    neo_js_variable_t res =
        neo_js_variable_def_accessor(object, ctx, key, NULL, value, true, true);
    NEO_JS_VM_CHECK(vm, res, program, offset);
  } else {
    prop->set = value->value;
    neo_js_handle_add_parent(&value->value->handle, &object->value->handle);
  }
}
static void neo_js_set_getter(neo_js_vm_t vm, neo_js_context_t ctx,
                              neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t key = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t object = neo_js_vm_get_value(vm);
  neo_js_object_property_t prop =
      neo_js_variable_get_own_property(object, ctx, key);
  if (!prop) {
    neo_js_variable_t res =
        neo_js_variable_def_accessor(object, ctx, key, value, NULL, true, true);
    NEO_JS_VM_CHECK(vm, res, program, offset);
  } else {
    prop->get = value->value;
    neo_js_handle_add_parent(&value->value->handle, &object->value->handle);
  }
}
static void neo_js_set_method(neo_js_vm_t vm, neo_js_context_t ctx,
                              neo_program_t program, size_t *offset) {
  neo_js_variable_t method = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t key = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t object = neo_js_vm_get_value(vm);
  neo_js_variable_t res = neo_js_variable_set_field(object, ctx, key, method);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}
static void neo_js_vm_jnull(neo_js_vm_t vm, neo_js_context_t ctx,
                            neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  size_t address = neo_js_vm_read_address(program, offset);
  if (value->value->type == NEO_JS_TYPE_NULL ||
      value->value->type == NEO_JS_TYPE_UNDEFINED) {
    *offset = address;
  }
}
static void neo_js_vm_jnot_null(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  size_t address = neo_js_vm_read_address(program, offset);
  if (value->value->type != NEO_JS_TYPE_NULL &&
      value->value->type != NEO_JS_TYPE_UNDEFINED) {
    *offset = address;
  }
}
static void neo_js_vm_jfalse(neo_js_vm_t vm, neo_js_context_t ctx,
                             neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  size_t address = neo_js_vm_read_address(program, offset);
  value = neo_js_variable_to_boolean(value, ctx);
  NEO_JS_VM_CHECK(vm, value, program, offset);
  if (!((neo_js_boolean_t)value->value)->value) {
    *offset = address;
  }
}

static void neo_js_vm_jtrue(neo_js_vm_t vm, neo_js_context_t ctx,
                            neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  value = neo_js_variable_to_boolean(value, ctx);
  size_t address = neo_js_vm_read_address(program, offset);
  NEO_JS_VM_CHECK(vm, value, program, offset);
  if (((neo_js_boolean_t)value->value)->value) {
    *offset = address;
  }
}

static void neo_js_vm_jmp(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  size_t address = neo_js_vm_read_address(program, offset);
  *offset = address;
}
static void neo_js_vm_break(neo_js_vm_t vm, neo_js_context_t ctx,
                            neo_program_t program, size_t *offset) {
  const char *label = neo_js_vm_read_string(program, offset);
  vm->result = neo_js_context_create_signal(ctx, NEO_JS_SIGNAL_BREAK, label);
  *offset = neo_buffer_get_size(program->codes);
}
static void neo_js_vm_continue(neo_js_vm_t vm, neo_js_context_t ctx,
                               neo_program_t program, size_t *offset) {
  const char *label = neo_js_vm_read_string(program, offset);
  vm->result = neo_js_context_create_signal(ctx, NEO_JS_SIGNAL_CONTINUE, label);
  *offset = neo_buffer_get_size(program->codes);
}
static void neo_js_vm_throw(neo_js_vm_t vm, neo_js_context_t ctx,
                            neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  vm->result = neo_js_context_create_exception(ctx, value);
  *offset = neo_buffer_get_size(program->codes);
}
static void neo_js_vm_try_begin(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  size_t onerror = neo_js_vm_read_address(program, offset);
  size_t onfinish = neo_js_vm_read_address(program, offset);
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_try_frame_t frame =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_try_frame_t), NULL);
  frame->onerror = onerror;
  frame->onfinish = onfinish;
  frame->stacktop = neo_list_get_size(vm->stack);
  frame->result = neo_js_context_get_undefined(ctx);
  frame->scope = neo_js_context_get_scope(ctx);
  frame->labelstack_top = neo_list_get_size(vm->labelstack);
  neo_list_push(vm->trystack, frame);
}
static void neo_js_vm_try_end(neo_js_vm_t vm, neo_js_context_t ctx,
                              neo_program_t program, size_t *offset) {
  neo_list_node_t node = neo_list_get_last(vm->trystack);
  neo_js_try_frame_t frame = neo_list_node_get(node);
  vm->result = frame->result;
  if (frame->onfinish) {
    *offset = frame->onfinish;
    frame->onfinish = 0;
  } else {
    if (vm->result->value->type == NEO_JS_TYPE_EXCEPTION) {
      *offset = neo_buffer_get_size(program->codes);
    }
    neo_list_pop(vm->trystack);
  }
}

static void neo_js_vm_ret(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  vm->result = value;
  *offset = neo_buffer_get_size(program->codes);
}

static void neo_js_vm_hlt(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  *offset = neo_buffer_get_size(program->codes);
}
static void neo_js_vm_keys(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t keys = neo_js_variable_get_keys(value, ctx);
  NEO_JS_VM_CHECK(vm, keys, program, offset);
  neo_list_push(vm->stack, keys);
}
static void neo_js_vm_await(neo_js_vm_t vm, neo_js_context_t ctx,
                            neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t interrupt = neo_js_context_create_interrupt(
      ctx, value, *offset, program, vm, NEO_JS_INTERRUPT_AWAIT);
  vm->result = interrupt;
  *offset = neo_buffer_get_size(program->codes);
}
static void neo_js_vm_yield(neo_js_vm_t vm, neo_js_context_t ctx,
                            neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t interrupt = neo_js_context_create_interrupt(
      ctx, value, *offset, program, vm, NEO_JS_INTERRUPT_YIELD);
  vm->result = interrupt;
  *offset = neo_buffer_get_size(program->codes);
}
static void neo_js_vm_next(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  neo_js_variable_t iterator = neo_js_vm_get_value(vm);
  neo_js_variable_t next = neo_js_context_create_cstring(ctx, "next");
  next = neo_js_variable_get_field(iterator, ctx, next);
  neo_js_variable_t res = neo_js_variable_call(next, ctx, iterator, 0, NULL);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}

static void neo_js_vm_resolve_next(neo_js_vm_t vm, neo_js_context_t ctx,
                                   neo_program_t program, size_t *offset) {
  neo_js_variable_t obj = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t done = neo_js_variable_get_field(
      obj, ctx, neo_js_context_create_cstring(ctx, "done"));
  neo_js_variable_t value = neo_js_variable_get_field(
      obj, ctx, neo_js_context_create_cstring(ctx, "value"));
  neo_list_push(vm->stack, value);
  neo_list_push(vm->stack, done);
}

static void neo_js_vm_iterator(neo_js_vm_t vm, neo_js_context_t ctx,
                               neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t iterator =
      neo_js_variable_get_field(value, ctx, constant->symbol_iterator);
  NEO_JS_VM_CHECK(vm, iterator, program, offset);
  if (iterator->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not iterable", value);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    neo_js_variable_t exception = neo_js_context_create_exception(ctx, error);
    vm->result = exception;
    *offset = neo_buffer_get_size(program->codes);
    return;
  }
  iterator = neo_js_variable_call(iterator, ctx, value, 0, NULL);
  NEO_JS_VM_CHECK(vm, iterator, program, offset);
  neo_list_push(vm->stack, iterator);
}

static void neo_js_vm_async_iterator(neo_js_vm_t vm, neo_js_context_t ctx,
                                     neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t iterator =
      neo_js_variable_get_field(value, ctx, constant->symbol_async_iterator);
  NEO_JS_VM_CHECK(vm, iterator, program, offset);
  if (iterator->value->type != NEO_JS_TYPE_FUNCTION) {
    iterator = neo_js_variable_get_field(value, ctx, constant->symbol_iterator);
    NEO_JS_VM_CHECK(vm, iterator, program, offset);
  }
  if (iterator->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not iterable", value);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    neo_js_variable_t exception = neo_js_context_create_exception(ctx, error);
    vm->result = exception;
    *offset = neo_buffer_get_size(program->codes);
    return;
  }
  iterator = neo_js_variable_call(iterator, ctx, value, 0, NULL);
  NEO_JS_VM_CHECK(vm, iterator, program, offset);
  neo_list_push(vm->stack, iterator);
}

static void neo_js_vm_rest(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  neo_js_variable_t array = neo_js_context_create_array(ctx);
  neo_js_variable_t iterator = neo_js_vm_get_value(vm);
  neo_js_variable_t next = neo_js_context_create_cstring(ctx, "next");
  next = neo_js_variable_get_field(iterator, ctx, next);
  NEO_JS_VM_CHECK(vm, next, program, offset);
  neo_js_variable_t gen = neo_js_variable_call(next, ctx, iterator, 0, NULL);
  NEO_JS_VM_CHECK(vm, gen, program, offset);
  neo_js_variable_t key_done = neo_js_context_create_cstring(ctx, "done");
  neo_js_variable_t key_value = neo_js_context_create_cstring(ctx, "value");
  neo_js_variable_t done = neo_js_variable_get_field(gen, ctx, key_done);
  if (done->value->type != NEO_JS_TYPE_BOOLEAN) {
    done = neo_js_variable_to_boolean(done, ctx);
  }
  NEO_JS_VM_CHECK(vm, done, program, offset);
  while (!((neo_js_boolean_t)done->value)->value) {
    neo_js_variable_t value = neo_js_variable_get_field(gen, ctx, key_value);
    NEO_JS_VM_CHECK(vm, value, program, offset);
    neo_js_variable_t res = neo_js_array_push(ctx, array, 1, &value);
    NEO_JS_VM_CHECK(vm, res, program, offset);
    gen = neo_js_variable_call(next, ctx, iterator, 0, NULL);
    NEO_JS_VM_CHECK(vm, gen, program, offset);
    done = neo_js_variable_get_field(gen, ctx, key_done);
    if (done->value->type != NEO_JS_TYPE_BOOLEAN) {
      done = neo_js_variable_to_boolean(done, ctx);
    }
  }
  neo_list_push(vm->stack, array);
}
static void neo_js_vm_rest_object(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  neo_js_variable_t used_keys = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t obj = neo_js_vm_get_value(vm);
  neo_js_variable_t keys = neo_js_variable_get_keys(obj, ctx);
  neo_js_variable_t res = neo_js_context_create_object(ctx, NULL);
  neo_js_variable_t len = neo_js_variable_get_field(
      keys, ctx, neo_js_context_create_cstring(ctx, "length"));
  uint32_t length = ((neo_js_number_t)len->value)->value;
  len = neo_js_variable_get_field(used_keys, ctx,
                                  neo_js_context_create_cstring(ctx, "length"));
  uint32_t used_length = ((neo_js_number_t)len->value)->value;
  for (uint32_t idx = 0; idx < length; idx++) {
    neo_js_variable_t key = neo_js_variable_get_field(
        keys, ctx, neo_js_context_create_number(ctx, idx));
    uint32_t idx2 = 0;
    for (; idx2 < used_length; idx2++) {
      neo_js_variable_t key2 = neo_js_variable_get_field(
          used_keys, ctx, neo_js_context_create_number(ctx, idx2));
      const uint16_t *src = ((neo_js_string_t)key->value)->value;
      const uint16_t *dst = ((neo_js_string_t)key2->value)->value;
      if (neo_string16_compare(src, dst) == 0) {
        break;
      }
    }
    if (idx2 == used_length) {
      neo_js_variable_set_field(res, ctx, key,
                                neo_js_variable_get_field(obj, ctx, key));
    }
  }
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_new(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  int32_t line = neo_js_vm_read_integer(program, offset);
  int32_t column = neo_js_vm_read_integer(program, offset);
  neo_js_variable_t argument = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t callable = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  if (callable->value->type != NEO_JS_TYPE_FUNCTION ||
      ((neo_js_callable_t)callable->value)->lambda ||
      ((neo_js_callable_t)callable->value)->async ||
      ((neo_js_callable_t)callable->value)->generator) {
    neo_js_variable_t message =
        neo_js_context_format(ctx, "%v is not a constructor", callable);
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    vm->result = neo_js_context_create_exception(ctx, error);
    *offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_variable_t length = neo_js_variable_get_field(
      argument, ctx, neo_js_context_create_cstring(ctx, "length"));
  uint32_t argc = ((neo_js_number_t)length->value)->value;
  neo_js_variable_t argv[argc];
  for (uint32_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_variable_get_field(
        argument, ctx, neo_js_context_create_number(ctx, idx));
  }
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_variable_t name = neo_js_variable_get_field(
      callable, ctx, neo_js_context_create_cstring(ctx, "name"));
  const uint16_t *funcname = ((neo_js_string_t)name->value)->value;
  neo_js_context_push_callstack(ctx, program->filename, funcname, line, column);
  neo_js_variable_t result =
      neo_js_variable_construct(callable, ctx, argc, argv);
  neo_js_context_pop_callstack(ctx);
  NEO_JS_VM_CHECK(vm, result, program, offset);
  neo_list_push(vm->stack, result);
}

static void neo_js_vm_eq(neo_js_vm_t vm, neo_js_context_t ctx,
                         neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_eq(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_ne(neo_js_vm_t vm, neo_js_context_t ctx,
                         neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_eq(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  if (res == neo_js_context_get_true(ctx)) {
    res = neo_js_context_get_false(ctx);
  } else {
    res = neo_js_context_get_true(ctx);
  }
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_seq(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_seq(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_gt(neo_js_vm_t vm, neo_js_context_t ctx,
                         neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_gt(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_lt(neo_js_vm_t vm, neo_js_context_t ctx,
                         neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_lt(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_ge(neo_js_vm_t vm, neo_js_context_t ctx,
                         neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_ge(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_le(neo_js_vm_t vm, neo_js_context_t ctx,
                         neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_le(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_sne(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_seq(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  if (res == neo_js_context_get_true(ctx)) {
    res = neo_js_context_get_false(ctx);
  } else {
    res = neo_js_context_get_true(ctx);
  }
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_del(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_list_push(vm->stack, neo_js_context_get_true(ctx));
}
static void neo_js_vm_typeof(neo_js_vm_t vm, neo_js_context_t ctx,
                             neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t type = neo_js_variable_typeof(value, ctx);
  neo_list_push(vm->stack, type);
}
static void neo_js_vm_void(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t type = neo_js_context_get_undefined(ctx);
  neo_list_push(vm->stack, type);
}
static void neo_js_vm_inc(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_js_variable_t res = neo_js_variable_inc(value, ctx);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}
static void neo_js_vm_dec(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_js_variable_t res = neo_js_variable_dec(value, ctx);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}
static void neo_js_vm_defer_inc(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t another = neo_js_context_create_variable(ctx, value->value);
  neo_list_push(vm->stack, another);
  neo_js_variable_t res = neo_js_variable_inc(value, ctx);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}
static void neo_js_vm_defer_dec(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t value = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t another = neo_js_context_create_variable(ctx, value->value);
  neo_list_push(vm->stack, another);
  neo_js_variable_t res = neo_js_variable_dec(value, ctx);
  NEO_JS_VM_CHECK(vm, res, program, offset);
}
static void neo_js_vm_add(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_add(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_sub(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_sub(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_mul(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_mul(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_div(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_div(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_mod(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_mod(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_pow(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_div(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}

static void neo_js_vm_not(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_not(right, ctx);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}

static void neo_js_vm_and(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_and(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_or(neo_js_vm_t vm, neo_js_context_t ctx,
                         neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_or(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_xor(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_xor(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_shl(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_shl(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_shr(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_shr(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_ushr(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_ushr(left, ctx, right);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}

static void neo_js_vm_plus(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_plus(right, ctx);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}

static void neo_js_vm_neg(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_neg(right, ctx);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_logic_not(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  right = neo_js_variable_to_boolean(right, ctx);
  NEO_JS_VM_CHECK(vm, right, program, offset);
  bool value = ((neo_js_boolean_t)right->value)->value;
  neo_js_variable_t res =
      value ? neo_js_context_get_false(ctx) : neo_js_context_get_true(ctx);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_concat(neo_js_vm_t vm, neo_js_context_t ctx,
                             neo_program_t program, size_t *offset) {
  neo_js_variable_t left = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t right = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  if (left->value->type != NEO_JS_TYPE_STRING) {
    left = neo_js_variable_to_string(left, ctx);
    NEO_JS_VM_CHECK(vm, left, program, offset);
  }
  if (right->value->type != NEO_JS_TYPE_STRING) {
    right = neo_js_variable_to_string(left, ctx);
    NEO_JS_VM_CHECK(vm, right, program, offset);
  }
  const uint16_t *str1 = ((neo_js_string_t)left->value)->value;
  const uint16_t *str2 = ((neo_js_string_t)right->value)->value;
  size_t str1len = neo_string16_length(str1);
  size_t str2len = neo_string16_length(str2);
  uint16_t str[str1len + str2len + 1];
  memcpy(str, str1, str1len * sizeof(uint16_t));
  memcpy(str + str1len, str2, str2len);
  str[str1len + str2len] = 0;
  neo_js_variable_t res = neo_js_context_create_string(ctx, str);
  neo_list_push(vm->stack, res);
}

static void neo_js_vm_spread(neo_js_vm_t vm, neo_js_context_t ctx,
                             neo_program_t program, size_t *offset) {
  neo_js_variable_t current = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t target = neo_js_vm_get_value(vm);
  if (target->value->type == NEO_JS_TYPE_ARRAY) {
    neo_js_variable_t iterator =
        neo_js_context_get_constant(ctx)->symbol_iterator;
    iterator = neo_js_variable_get_field(current, ctx, iterator);
    if (iterator->value->type != NEO_JS_TYPE_FUNCTION) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "%v is not iterable", current);
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      vm->result = neo_js_context_create_exception(ctx, error);
      *offset = neo_buffer_get_size(program->codes);
      return;
    }
    iterator = neo_js_variable_call(iterator, ctx, current, 0, NULL);
    NEO_JS_VM_CHECK(vm, iterator, program, offset);
    if (iterator->value->type < NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "%v is not iterable", current);
      neo_js_variable_t type_error =
          neo_js_context_get_constant(ctx)->type_error_class;
      neo_js_variable_t error =
          neo_js_variable_construct(type_error, ctx, 1, &message);
      vm->result = neo_js_context_create_exception(ctx, error);
      *offset = neo_buffer_get_size(program->codes);
      return;
    }
    for (;;) {
      neo_js_variable_t next = neo_js_context_create_cstring(ctx, "next");
      next = neo_js_variable_get_field(iterator, ctx, next);
      NEO_JS_VM_CHECK(vm, next, program, offset);
      if (next->value->type != NEO_JS_TYPE_FUNCTION) {
        neo_js_variable_t message =
            neo_js_context_format(ctx, "%v is not iterable", current);
        neo_js_variable_t type_error =
            neo_js_context_get_constant(ctx)->type_error_class;
        neo_js_variable_t error =
            neo_js_variable_construct(type_error, ctx, 1, &message);
        vm->result = neo_js_context_create_exception(ctx, error);
        *offset = neo_buffer_get_size(program->codes);
        return;
      }
      neo_js_variable_t result =
          neo_js_variable_call(next, ctx, iterator, 0, NULL);
      NEO_JS_VM_CHECK(vm, result, program, offset);
      neo_js_variable_t done = neo_js_variable_get_field(
          result, ctx, neo_js_context_create_cstring(ctx, "done"));
      NEO_JS_VM_CHECK(vm, done, program, offset);
      done = neo_js_variable_to_boolean(done, ctx);
      NEO_JS_VM_CHECK(vm, done, program, offset);
      bool bldone = ((neo_js_boolean_t)done->value)->value;
      if (bldone) {
        break;
      }
      neo_js_variable_t value = neo_js_variable_get_field(
          result, ctx, neo_js_context_create_cstring(ctx, "value"));
      NEO_JS_VM_CHECK(vm, value, program, offset);
      neo_js_variable_t err = neo_js_array_push(ctx, target, 1, &value);
      NEO_JS_VM_CHECK(vm, err, program, offset);
    }
  } else if (target->value->type >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t keys = neo_js_variable_get_keys(current, ctx);
    NEO_JS_VM_CHECK(vm, keys, program, offset);
    neo_js_variable_t length = neo_js_variable_get_field(
        keys, ctx, neo_js_context_create_cstring(ctx, "length"));
    uint32_t len = ((neo_js_number_t)length->value)->value;
    for (uint32_t idx = 0; idx < len; idx++) {
      neo_js_variable_t key = neo_js_variable_get_field(
          keys, ctx, neo_js_context_create_number(ctx, idx));
      NEO_JS_VM_CHECK(vm, key, program, offset);
      neo_js_variable_t value = neo_js_variable_get_field(current, ctx, key);
      NEO_JS_VM_CHECK(vm, value, program, offset);
      neo_js_variable_t err =
          neo_js_variable_set_field(target, ctx, key, value);
      NEO_JS_VM_CHECK(vm, err, program, offset);
    }
  }
}

static void neo_js_vm_in(neo_js_vm_t vm, neo_js_context_t ctx,
                         neo_program_t program, size_t *offset) {
  neo_js_variable_t obj = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t key = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  if (obj->value->type < NEO_JS_TYPE_OBJECT) {
    obj = neo_js_variable_to_object(obj, ctx);
  }
  NEO_JS_VM_CHECK(vm, obj, program, offset);
  if (neo_js_variable_get_property(obj, ctx, key)) {
    neo_list_push(vm->stack, neo_js_context_get_true(ctx));
  } else {
    neo_list_push(vm->stack, neo_js_context_get_false(ctx));
  }
}

static void neo_js_vm_instance_of(neo_js_vm_t vm, neo_js_context_t ctx,
                                  neo_program_t program, size_t *offset) {
  neo_js_variable_t constructor = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t obj = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_instance_of(obj, ctx, constructor);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}
static void neo_js_vm_tag(neo_js_vm_t vm, neo_js_context_t ctx,
                          neo_program_t program, size_t *offset) {
  neo_js_variable_t argument = neo_js_vm_get_value(vm);
  neo_list_node_t it = neo_list_get_last(vm->stack);
  it = neo_list_node_last(it);
  neo_js_variable_t callable = neo_list_node_get(it);
  // TODO: callable == String.raw
  if (false) {
    return neo_js_vm_call(vm, ctx, program, offset);
  }
  neo_js_variable_t quasis = neo_js_variable_get_field(
      argument, ctx, neo_js_context_create_number(ctx, 0));
  neo_js_variable_t length = neo_js_variable_get_field(
      quasis, ctx, neo_js_context_create_cstring(ctx, "length"));
  uint32_t len = ((neo_js_number_t)length->value)->value;
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  for (uint32_t idx = 0; idx < len; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t str = neo_js_variable_get_field(quasis, ctx, index);
    const uint16_t *s = ((neo_js_string_t)str->value)->value;
    uint16_t *decode_string = neo_string16_decode(allocator, s);
    if (decode_string) {
      str = neo_js_context_create_string(ctx, decode_string);
    } else {
      str = neo_js_context_get_undefined(ctx);
    }
    neo_allocator_free(allocator, decode_string);
    neo_js_variable_set_field(quasis, ctx, index, str);
  }
  return neo_js_vm_call(vm, ctx, program, offset);
}

static void neo_js_vm_member_tag(neo_js_vm_t vm, neo_js_context_t ctx,
                                 neo_program_t program, size_t *offset) {
  neo_js_variable_t argument = neo_js_vm_get_value(vm);
  neo_list_node_t it = neo_list_get_last(vm->stack);
  it = neo_list_node_last(it);
  neo_js_variable_t key = neo_list_node_get(it);
  it = neo_list_node_last(it);
  neo_js_variable_t host = neo_list_node_get(it);
  neo_js_variable_t callable = neo_js_variable_get_field(host, ctx, key);
  NEO_JS_VM_CHECK(vm, callable, program, offset);
  // TODO: callable == String.raw
  if (false) {
    return neo_js_vm_call(vm, ctx, program, offset);
  }
  neo_js_variable_t quasis = neo_js_variable_get_field(
      argument, ctx, neo_js_context_create_number(ctx, 0));
  neo_js_variable_t length = neo_js_variable_get_field(
      quasis, ctx, neo_js_context_create_cstring(ctx, "length"));
  uint32_t len = ((neo_js_number_t)length->value)->value;
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  for (uint32_t idx = 0; idx < len; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t str = neo_js_variable_get_field(quasis, ctx, index);
    const uint16_t *s = ((neo_js_string_t)str->value)->value;
    uint16_t *decode_string = neo_string16_decode(allocator, s);
    if (decode_string) {
      str = neo_js_context_create_string(ctx, decode_string);
    } else {
      str = neo_js_context_get_undefined(ctx);
    }
    neo_allocator_free(allocator, decode_string);
    neo_js_variable_set_field(quasis, ctx, index, str);
  }
  return neo_js_vm_call(vm, ctx, program, offset);
}

static void neo_js_vm_del_field(neo_js_vm_t vm, neo_js_context_t ctx,
                                neo_program_t program, size_t *offset) {

  neo_js_variable_t key = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_js_vm_get_value(vm);
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_variable_del_field(host, ctx, key);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_list_push(vm->stack, res);
}

static neo_js_vm_handle_fn_t neo_js_vm_handles[] = {
    neo_js_vm_push_scope,           // NEO_ASM_PUSH_SCOPE
    neo_js_vm_pop_scope,            // NEO_ASM_POP_SCOPE
    neo_js_vm_pop,                  // NEO_ASM_POP
    neo_js_vm_store,                // NEO_ASM_STORE
    neo_js_vm_save,                 // NEO_ASM_SAVE
    neo_js_vm_def,                  // NEO_ASM_DEF
    neo_js_vm_load,                 // NEO_ASM_LOAD
    NULL,                           // NEO_ASM_INIT_ACCESSOR
    NULL,                           // NEO_ASM_INIT_PRIVATE_ACCESSOR
    NULL,                           // NEO_ASM_INIT_FIELD
    NULL,                           // NEO_ASM_INIT_PRIVATE_FIELD
    neo_js_vm_push_undefined,       // NEO_ASM_PUSH_UNDEFINED
    neo_js_vm_push_null,            // NEO_ASM_PUSH_NULL
    neo_js_vm_push_nan,             // NEO_ASM_PUSH_NAN
    neo_js_vm_push_infinity,        // NEO_ASM_PUSH_INFINTY
    neo_js_vm_push_uninitialized,   // NEO_ASM_PUSH_UNINITIALIZED
    neo_js_vm_push_true,            // NEO_ASM_PUSH_TRUE
    neo_js_vm_push_false,           // NEO_ASM_PUSH_FALSE
    neo_js_vm_push_number,          // NEO_ASM_PUSH_NUMBER
    neo_js_vm_push_string,          // NEO_ASM_PUSH_STRING
    neo_js_vm_push_bigint,          // NEO_ASM_PUSH_BIGINT
    NULL,                           // NEO_ASM_PUSH_REGEXP
    neo_js_vm_push_function,        // NEO_ASM_PUSH_FUNCTION
    NULL,                           // NEO_ASM_PUSH_CLASS
    neo_js_vm_push_async_function,  // NEO_ASM_PUSH_ASYNC_FUNCTION
    neo_js_vm_push_lambda,          // NEO_ASM_PUSH_LAMBDA
    neo_js_vm_push_async_lambda,    // NEO_ASM_PUSH_ASYNC_LAMBDA
    neo_js_vm_push_generator,       // NEO_ASM_PUSH_GENERATOR
    neo_js_vm_push_async_generator, // NEO_ASM_PUSH_ASYNC_GENERATOR
    neo_js_vm_push_object,          // NEO_ASM_PUSH_OBJECT
    neo_js_vm_push_array,           // NEO_ASM_PUSH_ARRAY
    neo_js_vm_push_this,            // NEO_ASM_PUSH_THIS
    NULL,                           // NEO_ASM_SUPER_CALL
    NULL,                           // NEO_ASM_SUPER_MEMBER_CALL
    NULL,                           // NEO_ASM_GET_SUPER_FIELD
    NULL,                           // NEO_ASM_SET_SUPER_FIELD
    neo_js_vm_push_value,           // NEO_ASM_PUSH_VALUE
    neo_js_vm_push_break_label,     // NEO_ASM_PUSH_BREAK_LABEL
    neo_js_vm_push_continue_label,  // NEO_ASM_PUSH_CONTINUE_LABEL
    neo_js_vm_pop_label,            // NEO_ASM_POP_LABEL
    neo_js_vm_set_const,            // NEO_ASM_SET_CONST
    neo_js_vm_set_using,            // NEO_ASM_SET_USING
    neo_js_vm_set_await_using,      // NEO_ASM_SET_AWAIT_USING
    neo_js_vm_set_source,           // NEO_ASM_SET_SOURCE
    NULL,                           // NEO_ASM_SET_BIND
    NULL,                           // NEO_ASM_SET_CLASS
    neo_js_vm_set_address,          // NEO_ASM_SET_ADDRESS
    neo_js_vm_set_name,             // NEO_ASM_SET_NAME
    neo_js_vm_set_closure,          // NEO_ASM_SET_CLOSURE
    NULL,                           // NEO_ASM_EXTENDS
    NULL,                           // NEO_ASM_DECORATOR
    neo_js_vm_directive,            // NEO_ASM_DIRECTIVE
    neo_js_vm_call,                 // NEO_ASM_CALL
    neo_js_vm_push_back,            // NEO_ASM_APPEND
    NULL,                           // NEO_ASM_EVAL
    neo_js_vm_member_call,          // NEO_ASM_MEMBER_CALL
    neo_js_get_field,               // NEO_ASM_GET_FIELD
    neo_js_set_field,               // NEO_ASM_SET_FIELD
    NULL,                           // NEO_ASM_PRIVATE_CALL
    NULL,                           // NEO_ASM_GET_PRIVATE_FIELD
    NULL,                           // NEO_ASM_SET_PRIVATE_FIELD
    neo_js_set_getter,              // NEO_ASM_SET_GETTER
    neo_js_set_setter,              // NEO_ASM_SET_SETTER
    neo_js_set_method,              // NEO_ASM_SET_METHOD
    NULL,                           // NEO_ASM_DEF_PRIVATE_GETTER
    NULL,                           // NEO_ASM_DEF_PRIVATE_SETTER
    NULL,                           // NEO_ASM_DEF_PRIVATE_METHOD
    neo_js_vm_jnull,                // NEO_ASM_JNULL
    neo_js_vm_jnot_null,            // NEO_ASM_JNOT_NULL
    neo_js_vm_jfalse,               // NEO_ASM_JFALSE
    neo_js_vm_jtrue,                // NEO_ASM_JTRUE
    neo_js_vm_jmp,                  // NEO_ASM_JMP
    neo_js_vm_break,                // NEO_ASM_BREAK
    neo_js_vm_continue,             // NEO_ASM_CONTINUE
    neo_js_vm_throw,                // NEO_ASM_THROW
    neo_js_vm_try_begin,            // NEO_ASM_TRY_BEGIN
    neo_js_vm_try_end,              // NEO_ASM_TRY_END
    neo_js_vm_ret,                  // NEO_ASM_RET
    neo_js_vm_hlt,                  // NEO_ASM_HLT
    neo_js_vm_keys,                 // NEO_ASM_KEYS
    neo_js_vm_await,                // NEO_ASM_AWAIT
    neo_js_vm_yield,                // NEO_ASM_YIELD
    neo_js_vm_next,                 // NEO_ASM_NEXT
    neo_js_vm_resolve_next,         // NEO_ASM_RESOLVE_NEXT
    neo_js_vm_iterator,             // NEO_ASM_ITERATOR
    neo_js_vm_async_iterator,       // NEO_ASM_ASYNC_ITERATOR
    neo_js_vm_rest,                 // NEO_ASM_REST
    neo_js_vm_rest_object,          // NEO_ASM_REST_OBJECT
    NULL,                           // NEO_ASM_IMPORT
    NULL,                           // NEO_ASM_ASSERT
    NULL,                           // NEO_ASM_EXPORT
    NULL,                           // NEO_ASM_EXPORT_ALL
    NULL,                           // NEO_ASM_BREAKPOINT
    neo_js_vm_new,                  // NEO_ASM_NEW
    neo_js_vm_eq,                   // NEO_ASM_EQ
    neo_js_vm_ne,                   // NEO_ASM_NE
    neo_js_vm_seq,                  // NEO_ASM_SEQ
    neo_js_vm_gt,                   // NEO_ASM_GT
    neo_js_vm_lt,                   // NEO_ASM_LT
    neo_js_vm_ge,                   // NEO_ASM_GE
    neo_js_vm_le,                   // NEO_ASM_LE
    neo_js_vm_sne,                  // NEO_ASM_SNE
    neo_js_vm_del,                  // NEO_ASM_DEL
    neo_js_vm_typeof,               // NEO_ASM_TYPEOF
    neo_js_vm_void,                 // NEO_ASM_VOID
    neo_js_vm_inc,                  // NEO_ASM_INC
    neo_js_vm_dec,                  // NEO_ASM_DEC
    neo_js_vm_defer_inc,            // NEO_ASM_DEFER_INC
    neo_js_vm_defer_dec,            // NEO_ASM_DEFER_DEC
    neo_js_vm_add,                  // NEO_ASM_ADD
    neo_js_vm_sub,                  // NEO_ASM_SUB
    neo_js_vm_mul,                  // NEO_ASM_MUL
    neo_js_vm_div,                  // NEO_ASM_DIV
    neo_js_vm_mod,                  // NEO_ASM_MOD
    neo_js_vm_pow,                  // NEO_ASM_POW
    neo_js_vm_not,                  // NEO_ASM_NOT
    neo_js_vm_and,                  // NEO_ASM_AND
    neo_js_vm_or,                   // NEO_ASM_OR
    neo_js_vm_xor,                  // NEO_ASM_XOR
    neo_js_vm_shr,                  // NEO_ASM_SHR
    neo_js_vm_shl,                  // NEO_ASM_SHL
    neo_js_vm_ushr,                 // NEO_ASM_USHR
    neo_js_vm_plus,                 // NEO_ASM_PLUS
    neo_js_vm_neg,                  // NEO_ASM_NEG
    neo_js_vm_logic_not,            // NEO_ASM_LOGICAL_NOT
    neo_js_vm_concat,               // NEO_ASM_CONCAT
    neo_js_vm_spread,               // NEO_ASM_SPREAD
    neo_js_vm_in,                   // NEO_ASM_IN
    neo_js_vm_instance_of,          // NEO_ASM_INSTANCE_OF
    neo_js_vm_tag,                  // NEO_ASM_TAG
    neo_js_vm_member_tag,           // NEO_ASM_MEMBER_TAG
    NULL,                           // NEO_ASM_PRIVATE_TAG
    NULL,                           // NEO_ASM_SUPER_MEMBER_TAG
    neo_js_vm_del_field,            // NEO_ASM_DEL_FIELD
};

static bool neo_js_vm_resolve_signal(neo_js_vm_t vm, neo_js_context_t ctx,
                                     neo_program_t program, size_t *offset) {
  neo_js_signal_t signal = (neo_js_signal_t)vm->result->value;
  if (signal->type != NEO_JS_SIGNAL_BREAK ||
      signal->type != NEO_JS_SIGNAL_CONTINUE) {
    return false;
  }
  const char *name = (const char *)signal->msg;
  while (neo_list_get_size(vm->labelstack)) {
    neo_list_node_t node = neo_list_get_last(vm->labelstack);
    neo_js_label_frame_t frame = neo_list_node_get(node);
    while (frame->trystack_top != neo_list_get_size(vm->trystack)) {
      neo_list_node_t node = neo_list_get_last(vm->trystack);
      neo_js_try_frame_t tryframe = neo_list_node_get(node);
      if (tryframe->onfinish) {
        *offset = tryframe->onfinish;
        tryframe->onfinish = neo_buffer_get_size(program->codes);
        tryframe->result = vm->result;
        return true;
      }
      neo_list_pop(vm->trystack);
    }
    while (neo_list_get_size(vm->stack) != frame->stacktop) {
      neo_list_pop(vm->stack);
    }
    while (neo_js_context_get_scope(ctx) != frame->scope) {
      neo_js_vm_pop_scope(vm, ctx, program, offset);
    }
    if ((signal->type == NEO_JS_SIGNAL_BREAK &&
         frame->type == NEO_JS_LABEL_BREAK) ||
        (signal->type == NEO_JS_SIGNAL_CONTINUE &&
         frame->type == NEO_JS_LABEL_CONTINUE)) {
      if (strcmp(frame->name, name) == 0) {
        *offset = frame->address;
        vm->result = neo_js_context_get_undefined(ctx);
        return true;
      }
    }
    neo_list_pop(vm->labelstack);
  }
  return false;
}

static bool neo_js_vm_resolve_result(neo_js_vm_t vm, neo_js_context_t ctx,
                                     neo_program_t program, size_t *offset) {
  if (vm->result->value->type == NEO_JS_TYPE_SIGNAL) {
    if (neo_js_vm_resolve_signal(vm, ctx, program, offset)) {
      return false;
    }
  }
  while (neo_list_get_size(vm->trystack)) {
    neo_list_node_t node = neo_list_get_last(vm->trystack);
    neo_js_try_frame_t frame = neo_list_node_get(node);
    while (neo_js_context_get_scope(ctx) != frame->scope) {
      neo_js_vm_pop_scope(vm, ctx, program, offset);
    }
    while (neo_list_get_size(vm->stack) != frame->stacktop) {
      neo_list_pop(vm->stack);
    }
    while (neo_list_get_size(vm->labelstack) != frame->labelstack_top) {
      neo_list_pop(vm->labelstack);
    }
    size_t onfinish = frame->onfinish;
    size_t onerror = frame->onerror;
    if (onerror && vm->result->value->type == NEO_JS_TYPE_EXCEPTION) {
      *offset = frame->onerror;
      frame->onerror = 0;
      neo_js_variable_t error = neo_js_context_create_variable(
          ctx, ((neo_js_exception_t)vm->result->value)->error);
      neo_list_push(vm->stack, error);
      vm->result = neo_js_context_get_undefined(ctx);
      return false;
    }
    if (onfinish) {
      frame->result = vm->result;
      vm->result = neo_js_context_get_undefined(ctx);
      *offset = onfinish;
      frame->onfinish = 0;
      return false;
    }
    neo_list_pop(vm->trystack);
  }
  return true;
}

neo_js_variable_t neo_js_vm_run(neo_js_vm_t self, neo_js_context_t ctx,
                                neo_program_t program, size_t offset) {
  for (;;) {
    if (offset == neo_buffer_get_size(program->codes)) {
      if (self->result->value->type != NEO_JS_TYPE_INTERRUPT) {
        if (!neo_js_vm_resolve_result(self, ctx, program, &offset)) {
          continue;
        }
      }
      break;
    }
    neo_asm_code_t code = neo_js_vm_read_code(program, &offset);
    neo_js_vm_handle_fn_t handle = neo_js_vm_handles[(size_t)code];
    if (handle) {
      handle(self, ctx, program, &offset);
    } else {
      neo_js_variable_t error =
          neo_js_context_create_cstring(ctx, "not implement");
      self->result = neo_js_context_create_exception(ctx, error);
      offset = neo_buffer_get_size(program->codes);
    }
  }
  neo_js_variable_t result = self->result;
  self->result = NULL;
  return result;
}