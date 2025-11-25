#include "runtime/vm.h"
#include "compiler/asm.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/common.h"
#include "core/list.h"
#include "engine/boolean.h"
#include "engine/callable.h"
#include "engine/context.h"
#include "engine/exception.h"
#include "engine/function.h"
#include "engine/number.h"
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
    neo_js_variable_t error = (expression);                                    \
    if (error->value->type == NEO_JS_TYPE_EXCEPTION) {                         \
      vm->result = error;                                                      \
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

struct _neo_js_vm_t {
  neo_list_t stack;
  neo_list_t trystack;
  neo_list_t labelstack;
  neo_js_variable_t result;
  neo_js_variable_t self;
};

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
  neo_js_variable_t value = neo_js_context_create_bigint(ctx, string);
  neo_list_push(vm->stack, value);
}

static void neo_js_vm_push_function(neo_js_vm_t vm, neo_js_context_t ctx,
                                    neo_program_t program, size_t *offset) {
  neo_js_variable_t function = neo_js_context_create_function(ctx, program);
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
  const uint16_t *funcname = NULL;
  if (name->value->type == NEO_JS_TYPE_STRING) {
    funcname = ((neo_js_string_t)name->value)->value;
  }
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

static void neo_js_vm_next(neo_js_vm_t vm, neo_js_context_t ctx,
                           neo_program_t program, size_t *offset) {
  neo_js_variable_t iterator = neo_js_vm_get_value(vm);
  neo_js_variable_t next = neo_js_context_create_cstring(ctx, "next");
  next = neo_js_variable_get_field(iterator, ctx, next);
  neo_js_variable_t res = neo_js_variable_call(next, ctx, iterator, 0, NULL);
  NEO_JS_VM_CHECK(vm, res, program, offset);
  neo_js_variable_t value = neo_js_variable_get_field(
      res, ctx, neo_js_context_create_cstring(ctx, "value"));
  NEO_JS_VM_CHECK(vm, value, program, offset);
  neo_js_variable_t done = neo_js_variable_get_field(
      res, ctx, neo_js_context_create_cstring(ctx, "done"));
  NEO_JS_VM_CHECK(vm, done, program, offset);
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

static neo_js_vm_handle_fn_t neo_js_vm_handles[] = {
    neo_js_vm_push_scope,          // NEO_ASM_PUSH_SCOPE
    neo_js_vm_pop_scope,           // NEO_ASM_POP_SCOPE
    neo_js_vm_pop,                 // NEO_ASM_POP
    neo_js_vm_store,               // NEO_ASM_STORE
    neo_js_vm_save,                // NEO_ASM_SAVE
    neo_js_vm_def,                 // NEO_ASM_DEF
    neo_js_vm_load,                // NEO_ASM_LOAD
    NULL,                          // NEO_ASM_CLONE
    NULL,                          // NEO_ASM_INIT_ACCESSOR
    NULL,                          // NEO_ASM_INIT_PRIVATE_ACCESSOR
    NULL,                          // NEO_ASM_INIT_FIELD
    NULL,                          // NEO_ASM_INIT_PRIVATE_FIELD
    neo_js_vm_push_undefined,      // NEO_ASM_PUSH_UNDEFINED
    neo_js_vm_push_null,           // NEO_ASM_PUSH_NULL
    neo_js_vm_push_nan,            // NEO_ASM_PUSH_NAN
    neo_js_vm_push_infinity,       // NEO_ASM_PUSH_INFINTY
    neo_js_vm_push_uninitialized,  // NEO_ASM_PUSH_UNINITIALIZED
    neo_js_vm_push_true,           // NEO_ASM_PUSH_TRUE
    neo_js_vm_push_false,          // NEO_ASM_PUSH_FALSE
    neo_js_vm_push_number,         // NEO_ASM_PUSH_NUMBER
    neo_js_vm_push_string,         // NEO_ASM_PUSH_STRING
    neo_js_vm_push_bigint,         // NEO_ASM_PUSH_BIGINT
    NULL,                          // NEO_ASM_PUSH_REGEXP
    neo_js_vm_push_function,       // NEO_ASM_PUSH_FUNCTION
    NULL,                          // NEO_ASM_PUSH_CLASS
    NULL,                          // NEO_ASM_PUSH_ASYNC_FUNCTION
    neo_js_vm_push_lambda,         // NEO_ASM_PUSH_LAMBDA
    NULL,                          // NEO_ASM_PUSH_ASYNC_LAMBDA
    NULL,                          // NEO_ASM_PUSH_GENERATOR
    NULL,                          // NEO_ASM_PUSH_ASYNC_GENERATOR
    neo_js_vm_push_object,         // NEO_ASM_PUSH_OBJECT
    neo_js_vm_push_array,          // NEO_ASM_PUSH_ARRAY
    neo_js_vm_push_this,           // NEO_ASM_PUSH_THIS
    NULL,                          // NEO_ASM_SUPER_CALL
    NULL,                          // NEO_ASM_SUPER_MEMBER_CALL
    NULL,                          // NEO_ASM_GET_SUPER_FIELD
    NULL,                          // NEO_ASM_SET_SUPER_FIELD
    neo_js_vm_push_value,          // NEO_ASM_PUSH_VALUE
    neo_js_vm_push_break_label,    // NEO_ASM_PUSH_BREAK_LABEL
    neo_js_vm_push_continue_label, // NEO_ASM_PUSH_CONTINUE_LABEL
    neo_js_vm_pop_label,           // NEO_ASM_POP_LABEL
    neo_js_vm_set_const,           // NEO_ASM_SET_CONST
    neo_js_vm_set_using,           // NEO_ASM_SET_USING
    neo_js_vm_set_await_using,     // NEO_ASM_SET_AWAIT_USING
    neo_js_vm_set_source,          // NEO_ASM_SET_SOURCE
    NULL,                          // NEO_ASM_SET_BIND
    NULL,                          // NEO_ASM_SET_CLASS
    neo_js_vm_set_address,         // NEO_ASM_SET_ADDRESS
    neo_js_vm_set_name,            // NEO_ASM_SET_NAME
    neo_js_vm_set_closure,         // NEO_ASM_SET_CLOSURE
    NULL,                          // NEO_ASM_EXTENDS
    NULL,                          // NEO_ASM_DECORATOR
    neo_js_vm_directive,           // NEO_ASM_DIRECTIVE
    neo_js_vm_call,                // NEO_ASM_CALL
    neo_js_vm_push_back,           // NEO_ASM_PUSH_BACK
    NULL,                          // NEO_ASM_EVAL
    NULL,                          // NEO_ASM_MEMBER_CALL
    neo_js_get_field,              // NEO_ASM_GET_FIELD
    neo_js_set_field,              // NEO_ASM_SET_FIELD
    NULL,                          // NEO_ASM_PRIVATE_CALL
    NULL,                          // NEO_ASM_GET_PRIVATE_FIELD
    NULL,                          // NEO_ASM_SET_PRIVATE_FIELD
    NULL,                          // NEO_ASM_SET_GETTER
    NULL,                          // NEO_ASM_SET_SETTER
    NULL,                          // NEO_ASM_SET_METHOD
    NULL,                          // NEO_ASM_DEF_PRIVATE_GETTER
    NULL,                          // NEO_ASM_DEF_PRIVATE_SETTER
    NULL,                          // NEO_ASM_DEF_PRIVATE_METHOD
    neo_js_vm_jnull,               // NEO_ASM_JNULL
    neo_js_vm_jnot_null,           // NEO_ASM_JNOT_NULL
    neo_js_vm_jfalse,              // NEO_ASM_JFALSE
    neo_js_vm_jtrue,               // NEO_ASM_JTRUE
    neo_js_vm_jmp,                 // NEO_ASM_JMP
    neo_js_vm_break,               // NEO_ASM_BREAK
    neo_js_vm_continue,            // NEO_ASM_CONTINUE
    neo_js_vm_throw,               // NEO_ASM_THROW
    neo_js_vm_try_begin,           // NEO_ASM_TRY_BEGIN
    neo_js_vm_try_end,             // NEO_ASM_TRY_END
    neo_js_vm_ret,                 // NEO_ASM_RET
    neo_js_vm_hlt,                 // NEO_ASM_HLT
    NULL,                          // NEO_ASM_KEYS
    NULL,                          // NEO_ASM_AWAIT
    NULL,                          // NEO_ASM_YIELD
    neo_js_vm_next,                // NEO_ASM_NEXT
    NULL,                          // NEO_ASM_AWAIT_NEXT
    NULL,                          // NEO_ASM_RESOLVE_NEXT
    neo_js_vm_iterator,            // NEO_ASM_ITERATOR
    NULL,                          // NEO_ASM_ASYNC_ITERATOR
    neo_js_vm_rest,                // NEO_ASM_REST
    NULL,                          // NEO_ASM_REST_OBJECT
    NULL,                          // NEO_ASM_IMPORT
    NULL,                          // NEO_ASM_ASSERT
    NULL,                          // NEO_ASM_EXPORT
    NULL,                          // NEO_ASM_EXPORT_ALL
    NULL,                          // NEO_ASM_BREAKPOINT
    neo_js_vm_new,                 // NEO_ASM_NEW
    NULL,                          // NEO_ASM_EQ
    NULL,                          // NEO_ASM_NE
    NULL,                          // NEO_ASM_SEQ
    NULL,                          // NEO_ASM_GT
    NULL,                          // NEO_ASM_LT
    NULL,                          // NEO_ASM_GE
    NULL,                          // NEO_ASM_LE
    NULL,                          // NEO_ASM_SNE
    NULL,                          // NEO_ASM_DEL
    NULL,                          // NEO_ASM_TYPEOF
    NULL,                          // NEO_ASM_VOID
    NULL,                          // NEO_ASM_INC
    NULL,                          // NEO_ASM_DEC
    NULL,                          // NEO_ASM_ADD
    NULL,                          // NEO_ASM_SUB
    NULL,                          // NEO_ASM_MUL
    NULL,                          // NEO_ASM_DIV
    NULL,                          // NEO_ASM_MOD
    NULL,                          // NEO_ASM_POW
    NULL,                          // NEO_ASM_NOT
    NULL,                          // NEO_ASM_AND
    NULL,                          // NEO_ASM_OR
    NULL,                          // NEO_ASM_XOR
    NULL,                          // NEO_ASM_SHR
    NULL,                          // NEO_ASM_SHL
    NULL,                          // NEO_ASM_USHR
    NULL,                          // NEO_ASM_PLUS
    NULL,                          // NEO_ASM_NEG
    NULL,                          // NEO_ASM_LOGICAL_NOT
    NULL,                          // NEO_ASM_CONCAT
    NULL,                          // NEO_ASM_SPREAD
    NULL,                          // NEO_ASM_IN
    NULL,                          // NEO_ASM_INSTANCE_OF
    NULL,                          // NEO_ASM_TAG
    NULL,                          // NEO_ASM_MEMBER_TAG
    NULL,                          // NEO_ASM_PRIVATE_TAG
    NULL,                          // NEO_ASM_SUPER_MEMBER_TAG
    NULL,                          // NEO_ASM_DEL_FIELD
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
  neo_js_scope_t current_scope = neo_js_context_get_scope(ctx);
  for (;;) {
    if (offset == neo_buffer_get_size(program->codes)) {
      if (self->result->value->type == NEO_JS_TYPE_INTERRUPT) {
        neo_js_scope_set_variable(current_scope, self->result, NULL);
        neo_js_context_set_scope(ctx, current_scope);
      } else {
        if (!neo_js_vm_resolve_result(self, ctx, program, &offset)) {
          continue;
        }
        neo_js_scope_set_variable(current_scope, self->result, NULL);
        while (neo_js_context_get_scope(ctx) != current_scope) {
          neo_js_context_pop_scope(ctx);
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