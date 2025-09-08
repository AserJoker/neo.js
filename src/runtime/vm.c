#include "runtime/vm.h"
#include "compiler/asm.h"
#include "compiler/parser.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/bigint.h"
#include "core/buffer.h"
#include "core/fs.h"
#include "core/list.h"
#include "core/path.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/callable.h"
#include "engine/basetype/error.h"
#include "engine/basetype/function.h"
#include "engine/basetype/interrupt.h"
#include "engine/basetype/number.h"
#include "engine/basetype/object.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "engine/std/string.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#define NEO_VM_CHECK_AND_THROW(expr, vm, program)                              \
  {                                                                            \
    neo_js_variable_t error##__LINE__ = (expr);                                \
    if (neo_js_variable_get_type(error##__LINE__)->kind ==                     \
        NEO_JS_TYPE_ERROR) {                                                   \
      neo_list_push((vm)->stack, error##__LINE__);                             \
      vm->offset = neo_buffer_get_size((program)->codes);                      \
      return;                                                                  \
    }                                                                          \
  }

typedef struct _neo_js_try_frame_t *neo_js_try_frame_t;
typedef struct _neo_js_label_frame_t *neo_js_label_frame_t;

struct _neo_js_try_frame_t {
  neo_js_scope_t scope;
  size_t stack_top;
  size_t label_stack_top;
  size_t onfinish;
  size_t onerror;
};

struct _neo_js_label_frame_t {
  enum { NEO_LABEL_BREAK, NEO_LABEL_CONTINUE } kind;
  neo_js_scope_t scope;
  size_t stack_top;
  size_t try_stack_top;
  const wchar_t *label;
  size_t addr;
};

static void neo_js_label_frame_dispose(neo_allocator_t allocator,
                                       neo_js_label_frame_t frame) {}

void neo_js_vm_dispose(neo_allocator_t allocator, neo_js_vm_t vm) {
  neo_allocator_free(allocator, vm->stack);
  neo_allocator_free(allocator, vm->try_stack);
  for (neo_list_node_t it = neo_list_get_first(vm->label_stack);
       it != neo_list_get_tail(vm->label_stack); it = neo_list_node_next(it)) {
    neo_js_label_frame_t frame = neo_list_node_get(it);
    neo_allocator_free(allocator, frame);
  }
  neo_allocator_free(allocator, vm->label_stack);
  neo_allocator_free(allocator, vm->active_features);
}

neo_js_try_frame_t neo_create_js_try_frame(neo_allocator_t allocator) {
  neo_js_try_frame_t frame =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_try_frame_t), NULL);
  frame->label_stack_top = 0;
  frame->stack_top = 0;
  frame->onerror = 0;
  frame->onfinish = 0;
  frame->stack_top = 0;
  frame->scope = NULL;
  return frame;
}

neo_js_vm_t neo_create_js_vm(neo_js_context_t ctx, neo_js_variable_t self,
                             neo_js_variable_t clazz, size_t offset,
                             neo_js_scope_t scope) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_vm_t vm = neo_allocator_alloc(allocator, sizeof(struct _neo_js_vm_t),
                                       neo_js_vm_dispose);
  vm->stack = neo_create_list(allocator, NULL);
  neo_list_initialize_t initialize = {true};
  vm->try_stack = neo_create_list(allocator, &initialize);
  vm->label_stack = neo_create_list(allocator, NULL);
  vm->ctx = ctx;
  vm->offset = offset;
  vm->self = self ? self : neo_js_context_create_undefined(ctx);
  vm->root = scope;
  vm->scope = scope;
  vm->clazz = clazz;
  vm->active_features = neo_create_list(allocator, NULL);
  vm->result = NULL;
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
  wchar_t *s = neo_string_to_wstring(allocator, str);
  neo_js_context_defer_free(vm->ctx, s);
  return s;
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

static neo_js_variable_t neo_js_vm_scope_dispose(neo_js_vm_t vm) {
  neo_js_variable_t result = NULL;
  neo_js_scope_t scope = neo_js_context_get_scope(vm->ctx);
  neo_js_scope_t parent = neo_js_scope_get_parent(scope);
  neo_list_t variables = neo_js_scope_get_variables(scope);
  neo_js_variable_t symbol = neo_js_context_get_std(vm->ctx).symbol_constructor;
  neo_js_variable_t dispose = neo_js_context_get_field(
      vm->ctx, symbol, neo_js_context_create_string(vm->ctx, L"dispose"), NULL);
  neo_js_variable_t asyncDispose = neo_js_context_get_field(
      vm->ctx, symbol, neo_js_context_create_string(vm->ctx, L"asyncDispose"),
      NULL);
  for (neo_list_node_t it = neo_list_get_first(variables);
       it != neo_list_get_tail(variables); it = neo_list_node_next(it)) {
    neo_js_variable_t variable = neo_list_node_get(it);
    if (neo_js_variable_is_await_using(variable) &&
        neo_js_variable_get_type(variable)->kind >= NEO_JS_TYPE_OBJECT) {
      neo_js_variable_set_await_using(variable, false);
      neo_js_variable_t dispose_fn =
          neo_js_context_get_field(vm->ctx, variable, asyncDispose, NULL);
      if (neo_js_variable_get_type(dispose_fn)->kind < NEO_JS_TYPE_CALLABLE) {
        dispose_fn = neo_js_context_get_field(vm->ctx, variable, dispose, NULL);
      }
      if (neo_js_variable_get_type(dispose_fn)->kind < NEO_JS_TYPE_CALLABLE) {
        continue;
      }
      neo_js_callable_t callble = neo_js_variable_to_callable(dispose_fn);
      neo_js_context_push_stackframe(vm->ctx, NULL, callble->name, 0, 0);
      neo_js_variable_t res =
          neo_js_context_call(vm->ctx, dispose_fn, variable, 0, NULL);
      neo_js_context_pop_stackframe(vm->ctx);
      if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_ERROR) {
        result = res;
      } else if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_INTERRUPT) {
        return res;
      } else if (neo_js_context_is_thenable(vm->ctx, res)) {
        return neo_js_context_create_interrupt(vm->ctx, res, vm->offset,
                                               NEO_JS_INTERRUPT_BACKEND_AWAIT);
      }
    }
    if (neo_js_variable_is_using(variable) &&
        neo_js_variable_get_type(variable)->kind >= NEO_JS_TYPE_OBJECT) {
      neo_js_variable_set_using(variable, false);
      neo_js_variable_t dispose_fn =
          neo_js_context_get_field(vm->ctx, variable, dispose, NULL);
      if (neo_js_variable_get_type(dispose_fn)->kind < NEO_JS_TYPE_CALLABLE) {
        continue;
      }
      neo_js_callable_t callble = neo_js_variable_to_callable(dispose_fn);
      neo_js_context_push_stackframe(vm->ctx, NULL, callble->name, 0, 0);
      neo_js_variable_t error =
          neo_js_context_call(vm->ctx, dispose_fn, variable, 0, NULL);
      neo_js_context_pop_stackframe(vm->ctx);
      if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
        result = error;
      }
    }
  }
  if (!result) {
    result = neo_js_context_create_undefined(vm->ctx);
  }
  result = neo_js_scope_create_variable(
      parent, neo_js_variable_get_chunk(result), NULL);
  return result;
}

typedef void (*neo_js_vm_cmd_fn_t)(neo_js_vm_t, neo_program_t);

void neo_js_vm_push_scope(neo_js_vm_t vm, neo_program_t program) {
  neo_js_context_push_scope(vm->ctx);
}

void neo_js_vm_pop_scope(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t res = neo_js_vm_scope_dispose(vm);
  if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_INTERRUPT) {
    neo_list_push(vm->stack, res);
    vm->offset = neo_buffer_get_size(program->codes);
  } else {
    neo_js_context_pop_scope(vm->ctx);
    if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_ERROR) {
      neo_list_push(vm->stack, res);
      vm->offset = neo_buffer_get_size(program->codes);
    }
  }
}

void neo_js_vm_pop(neo_js_vm_t vm, neo_program_t program) {
  if (!neo_list_get_size(vm->stack)) {
    neo_list_push(vm->stack,
                  neo_js_context_create_simple_error(
                      vm->ctx, NEO_JS_ERROR_INTERNAL, 0, L"vm stack overflow"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_list_pop(vm->stack);
}

void neo_js_vm_store(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t current = neo_list_node_get(neo_list_get_last(vm->stack));
  NEO_VM_CHECK_AND_THROW(neo_js_context_store_variable(vm->ctx, current, name),
                         vm, program);
}

void neo_js_vm_save(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_chunk_t root = neo_js_scope_get_root_chunk(vm->root);
  neo_js_chunk_t current = neo_js_scope_get_root_chunk(vm->scope);
  if (vm->result) {
    neo_js_chunk_add_parent(vm->result, current);
    neo_js_chunk_remove_parent(vm->result, root);
  }
  vm->result = neo_js_variable_get_chunk(value);
  neo_js_chunk_add_parent(vm->result, root);
}

void neo_js_vm_def(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t current = neo_list_node_get(neo_list_get_last(vm->stack));
  NEO_VM_CHECK_AND_THROW(neo_js_context_def_variable(vm->ctx, current, name),
                         vm, program);
}

void neo_js_vm_load(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t variable = neo_js_context_load_variable(vm->ctx, name);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_UNINITIALIZE) {
    size_t len = wcslen(name) + 16;
    variable = neo_js_context_create_simple_error(
        vm->ctx, NEO_JS_ERROR_TYPE, len, L"'%ls' is not initialized", name);
  }
  neo_list_push(vm->stack, variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_clone(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  variable = neo_js_context_clone(vm->ctx, variable);
  neo_list_push(vm->stack, variable);
  if (neo_js_variable_get_type(variable)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_init_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_context_set_field(vm->ctx, host, field, value, NULL);
  if (neo_js_variable_get_type(value)->kind >= NEO_JS_TYPE_CALLABLE &&
      vm->clazz) {
    neo_js_callable_set_class(vm->ctx, value, vm->clazz);
  }
}
void neo_js_vm_init_private_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_context_def_private(vm->ctx, host, field, value);
  if (neo_js_variable_get_type(value)->kind >= NEO_JS_TYPE_CALLABLE &&
      vm->clazz) {
    neo_js_callable_set_class(vm->ctx, value, vm->clazz);
  }
}

void neo_js_vm_init_accessor(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_context_set_field(vm->ctx, host, field, value, NULL);
}

void neo_js_vm_init_private_accessor(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_context_def_private(vm->ctx, host, field, value);
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
  const wchar_t *value = neo_js_vm_read_string(vm, program);
  neo_list_push(vm->stack, neo_js_context_create_string(vm->ctx, value));
}
void neo_js_vm_push_bigint(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *value = neo_js_vm_read_string(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  wchar_t *format = neo_create_wstring(allocator, value);
  format[wcslen(value) - 1] = L'\0';
  neo_bigint_t bigint = neo_string_to_bigint(allocator, format);
  neo_allocator_free(allocator, format);
  neo_list_push(vm->stack, neo_js_context_create_bigint(vm->ctx, bigint));
}
void neo_js_vm_push_regexp(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *rule = neo_js_vm_read_string(vm, program);
  const wchar_t *flag = neo_js_vm_read_string(vm, program);
  neo_js_variable_t regexp = neo_js_context_get_std(vm->ctx).regexp_constructor;
  neo_js_variable_t argv[] = {
      neo_js_context_create_string(vm->ctx, rule),
      neo_js_context_create_string(vm->ctx, flag),
  };
  neo_js_variable_t result = neo_js_context_construct(vm->ctx, regexp, 2, argv);
  NEO_VM_CHECK_AND_THROW(result, vm, program);
  neo_list_push(vm->stack, result);
}

void neo_js_vm_push_function(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_function(vm->ctx, program));
}
void neo_js_vm_push_class(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t clazz = neo_js_context_create_function(vm->ctx, program);
  neo_list_push(vm->stack, clazz);
  neo_js_callable_t callable = neo_js_variable_to_callable(clazz);
  callable->is_class = true;
  neo_js_callable_set_class(vm->ctx, clazz, clazz);
}
void neo_js_vm_push_async_function(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack,
                neo_js_context_create_async_function(vm->ctx, program));
}
void neo_js_vm_push_generator(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack,
                neo_js_context_create_generator_function(vm->ctx, program));
}
void neo_js_vm_push_async_generator(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_async_generator_function(
                               vm->ctx, program));
}
void neo_js_vm_push_lambda(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t lambda = neo_js_context_create_function(vm->ctx, program);
  neo_list_push(vm->stack, lambda);
  NEO_VM_CHECK_AND_THROW(neo_js_callable_set_bind(vm->ctx, lambda, vm->self),
                         vm, program);
}
void neo_js_vm_push_async_lambda(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t lambda =
      neo_js_context_create_async_function(vm->ctx, program);
  neo_list_push(vm->stack, lambda);
  NEO_VM_CHECK_AND_THROW(neo_js_callable_set_bind(vm->ctx, lambda, vm->self),
                         vm, program);
}

void neo_js_vm_push_object(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_object(vm->ctx, NULL));
}
void neo_js_vm_push_array(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack, neo_js_context_create_array(vm->ctx));
}
void neo_js_vm_push_this(neo_js_vm_t vm, neo_program_t program) {
  neo_list_push(vm->stack,
                neo_js_context_create_variable(
                    vm->ctx, neo_js_variable_get_chunk(vm->self), NULL));
}

void neo_js_vm_super_call(neo_js_vm_t vm, neo_program_t program) {
  uint32_t line = neo_js_vm_read_integer(vm, program);
  uint32_t column = neo_js_vm_read_integer(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t args = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, args, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  size_t argc = num->number;
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_context_get_field(
        vm->ctx, args, neo_js_context_create_number(vm->ctx, idx), NULL);
  }
  neo_list_pop(vm->stack);
  neo_js_variable_t constructor =
      neo_js_object_get_constructor(vm->ctx, vm->self);
  neo_js_variable_t extend = neo_js_object_get_prototype(vm->ctx, constructor);

  neo_js_callable_t callable = neo_js_variable_to_callable(extend);
  neo_js_variable_t result = NULL;
  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  if (callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, callable->name, column,
                                   line);
    result = neo_js_context_simple_call(vm->ctx, extend, vm->self, argc, argv);
    neo_js_context_pop_stackframe(vm->ctx);
  } else if (!callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, NULL, column, line);
    result = neo_js_context_create_simple_error(vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                                L"super is not callable");
    neo_js_context_pop_stackframe(vm->ctx);
  }
  neo_allocator_free(allocator, argv);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    neo_list_push(vm->stack, result);
    vm->offset = neo_buffer_get_size(program->codes);
  } else if (neo_js_variable_get_type(result)->kind >= NEO_JS_TYPE_OBJECT) {
    neo_js_context_copy(vm->ctx, result, vm->self);
  }
}
void neo_js_vm_super_member_call(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t host = NULL;
  if (neo_js_variable_get_type(vm->self)->kind >= NEO_JS_TYPE_CALLABLE) {
    host = neo_js_object_get_prototype(vm->ctx, vm->self);
  } else {
    host = neo_js_object_get_prototype(vm->ctx, vm->self);
    host = neo_js_object_get_prototype(vm->ctx, host);
  }
  uint32_t line = neo_js_vm_read_integer(vm, program);
  uint32_t column = neo_js_vm_read_integer(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t args = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, args, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  size_t argc = num->number;
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_context_get_field(
        vm->ctx, args, neo_js_context_create_number(vm->ctx, idx), NULL);
  }
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);

  neo_js_variable_t callee =
      neo_js_context_get_field(vm->ctx, host, field, NULL);
  neo_js_callable_t callable = neo_js_variable_to_callable(callee);
  neo_js_variable_t result = NULL;

  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  if (callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, callable->name, column,
                                   line);
    result = neo_js_context_call(vm->ctx, callee, vm->self, argc, argv);
    neo_js_context_pop_stackframe(vm->ctx);
  } else if (!callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, NULL, column, line);
    result = neo_js_context_create_simple_error(vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                                L"variable is not callable");
    neo_js_context_pop_stackframe(vm->ctx);
  }
  neo_allocator_free(allocator, argv);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_get_super_member(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t host = NULL;
  if (neo_js_variable_get_type(vm->self)->kind >= NEO_JS_TYPE_CALLABLE) {
    host = neo_js_object_get_prototype(vm->ctx, vm->self);
  } else {
    host = neo_js_object_get_prototype(vm->ctx, vm->self);
    host = neo_js_object_get_prototype(vm->ctx, host);
  }
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t value =
      neo_js_context_get_field(vm->ctx, host, field, NULL);
  neo_list_push(vm->stack, value);
}

void neo_js_vm_push_value(neo_js_vm_t vm, neo_program_t program) {
  int32_t offset = neo_js_vm_read_integer(vm, program);
  neo_list_node_t it = neo_list_get_tail(vm->stack);
  for (int32_t idx = 0; idx < offset; idx++) {
    it = neo_list_node_last(it);
  }
  neo_js_variable_t variable = neo_list_node_get(it);
  neo_list_push(vm->stack, variable);
}
void neo_js_vm_push_break_label(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *label = neo_js_vm_read_string(vm, program);
  size_t breakaddr = neo_js_vm_read_address(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_label_frame_t frame =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_label_frame_t),
                          neo_js_label_frame_dispose);
  frame->label = label;
  frame->addr = breakaddr;
  frame->scope = neo_js_context_get_scope(vm->ctx);
  frame->stack_top = neo_list_get_size(vm->stack);
  frame->try_stack_top = neo_list_get_size(vm->try_stack);
  frame->kind = NEO_LABEL_BREAK;
  neo_list_push(vm->label_stack, frame);
}
void neo_js_vm_push_continue_label(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *label = neo_js_vm_read_string(vm, program);
  size_t breakaddr = neo_js_vm_read_address(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_label_frame_t frame =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_label_frame_t),
                          neo_js_label_frame_dispose);
  frame->label = label;
  frame->addr = breakaddr;
  frame->scope = neo_js_context_get_scope(vm->ctx);
  frame->stack_top = neo_list_get_size(vm->stack);
  frame->try_stack_top = neo_list_get_size(vm->try_stack);
  frame->kind = NEO_LABEL_CONTINUE;
  neo_list_push(vm->label_stack, frame);
}
void neo_js_vm_pop_label(neo_js_vm_t vm, neo_program_t program) {
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_label_frame_t label =
      neo_list_node_get(neo_list_get_last(vm->label_stack));
  neo_allocator_free(allocator, label);
  neo_list_pop(vm->label_stack);
}

void neo_js_vm_set_const(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_set_const(variable, true);
}

void neo_js_vm_set_using(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_set_using(variable, true);
}

void neo_js_vm_set_await_using(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_set_await_using(variable, true);
}

void neo_js_vm_set_address(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_JS_TYPE_FUNCTION) {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_SYNTAX, 0,
                                 L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  function->address = address;
}

void neo_js_vm_set_name(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_JS_TYPE_FUNCTION) {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  function->callable.name = neo_create_wstring(allocator, name);
}

void neo_js_vm_set_closure(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_JS_TYPE_FUNCTION) {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_variable_t value = neo_js_context_load_variable(vm->ctx, name);
  NEO_VM_CHECK_AND_THROW(value, vm, program);
  neo_js_callable_set_closure(vm->ctx, variable, name, value);
}
void neo_js_vm_extends(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t parent = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t self = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t prototype = neo_js_context_get_field(
      vm->ctx, self, neo_js_context_create_string(vm->ctx, L"prototype"), NULL);
  neo_js_variable_t proto = neo_js_context_get_field(
      vm->ctx, parent, neo_js_context_create_string(vm->ctx, L"prototype"),
      NULL);
  neo_js_object_set_prototype(vm->ctx, prototype, proto);
  neo_js_object_set_prototype(vm->ctx, self, parent);
}

void neo_js_vm_set_source(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *source = neo_js_vm_read_string(vm, program);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(variable)->kind != NEO_JS_TYPE_FUNCTION) {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not a function"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_function_t function = neo_js_variable_to_function(variable);
  function->source = source;
}

void neo_js_vm_set_bind(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t bind = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  NEO_VM_CHECK_AND_THROW(neo_js_callable_set_bind(vm->ctx, variable, bind), vm,
                         program);
}
void neo_js_vm_set_class(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t clazz = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  NEO_VM_CHECK_AND_THROW(neo_js_callable_set_class(vm->ctx, variable, clazz),
                         vm, program);
}

void neo_js_vm_direcitve(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *feature = neo_js_vm_read_string(vm, program);
  neo_js_context_enable(vm->ctx, feature);
  neo_list_push(vm->active_features, (void *)feature);
}

void neo_js_vm_call(neo_js_vm_t vm, neo_program_t program) {
  uint32_t line = neo_js_vm_read_integer(vm, program);
  uint32_t column = neo_js_vm_read_integer(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t args = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, args, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  size_t argc = num->number;
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_context_get_field(
        vm->ctx, args, neo_js_context_create_number(vm->ctx, idx), NULL);
  }
  neo_js_variable_t callee = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_callable_t callable = neo_js_variable_to_callable(callee);
  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  if (!callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, L"", column, line);
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not a function"));
    neo_js_context_pop_stackframe(vm->ctx);
    vm->offset = neo_buffer_get_size(program->codes);
    neo_allocator_free(allocator, argv);
    return;
  }
  neo_js_context_push_stackframe(vm->ctx, filename, callable->name, column,
                                 line);
  neo_js_variable_t result = neo_js_context_call(
      vm->ctx, callee, neo_js_context_create_undefined(vm->ctx), argc, argv);
  neo_js_context_pop_stackframe(vm->ctx);
  neo_allocator_free(allocator, argv);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_eval(neo_js_vm_t vm, neo_program_t program) {
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t args = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, args, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  size_t argc = num->number;
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_context_get_field(
        vm->ctx, args, neo_js_context_create_number(vm->ctx, idx), NULL);
  }

  neo_js_variable_t arg = NULL;
  if (argc) {
    arg = argv[0];
  } else {
    neo_list_push(vm->stack, neo_js_context_create_undefined(vm->ctx));
    return;
  }
  if (neo_js_variable_get_type(arg)->kind != NEO_JS_TYPE_STRING) {
    neo_list_push(vm->stack, arg);
    return;
  }
  const wchar_t *source = neo_js_variable_to_string(arg)->string;
  char *src = neo_wstring_to_string(allocator, source);
  neo_js_context_defer_free(vm->ctx, src);
  neo_ast_node_t node =
      TRY(neo_ast_parse_code(allocator, "<anonymouse_script>", src)) {
    neo_js_variable_t error = neo_js_context_create_compile_error(vm->ctx);
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  };
  neo_js_context_defer_free(vm->ctx, node);
  neo_program_t custom =
      TRY(neo_ast_write_node(allocator, "<anonymouse_script>", node)) {
    neo_js_variable_t error = neo_js_context_create_compile_error(vm->ctx);
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  };
  neo_js_context_defer_free(vm->ctx, custom);
  neo_js_vm_t custom_vm = neo_create_js_vm(vm->ctx, vm->self, vm->clazz, 0,
                                           neo_js_context_get_scope(vm->ctx));
  neo_js_context_defer_free(vm->ctx, custom_vm);
  neo_js_variable_t result = neo_js_vm_exec(custom_vm, custom);
  neo_allocator_free(allocator, argv);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_member_call(neo_js_vm_t vm, neo_program_t program) {
  uint32_t line = neo_js_vm_read_integer(vm, program);
  uint32_t column = neo_js_vm_read_integer(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t args = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, args, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  size_t argc = num->number;
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_context_get_field(
        vm->ctx, args, neo_js_context_create_number(vm->ctx, idx), NULL);
  }

  neo_js_variable_t callee =
      neo_js_context_get_field(vm->ctx, host, field, NULL);
  neo_js_callable_t callable = neo_js_variable_to_callable(callee);
  neo_js_variable_t result = NULL;
  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  if (callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, callable->name, column,
                                   line);
    result = neo_js_context_call(vm->ctx, callee, host, argc, argv);
    neo_js_context_pop_stackframe(vm->ctx);
  } else if (!callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, NULL, column, line);
    result = neo_js_context_create_simple_error(vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                                L"variable is not callable");
    neo_js_context_pop_stackframe(vm->ctx);
  }
  neo_allocator_free(allocator, argv);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_get_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t value =
      neo_js_context_get_field(vm->ctx, host, field, NULL);
  neo_list_push(vm->stack, value);
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_set_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  NEO_VM_CHECK_AND_THROW(
      neo_js_context_set_field(vm->ctx, host, field, value, NULL), vm, program);
  if (neo_js_variable_get_type(value)->kind >= NEO_JS_TYPE_CALLABLE &&
      vm->clazz) {
    neo_js_callable_set_class(vm->ctx, value, vm->clazz);
  }
}

void neo_js_vm_private_member_call(neo_js_vm_t vm, neo_program_t program) {
  uint32_t line = neo_js_vm_read_integer(vm, program);
  uint32_t column = neo_js_vm_read_integer(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t args = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_object_t obj = neo_js_variable_to_object(host);
  if (!obj->constructor || !vm->clazz ||
      neo_js_chunk_get_value(obj->constructor) !=
          neo_js_variable_get_value(vm->clazz)) {
    neo_js_string_t str = neo_js_variable_to_string(field);
    size_t len = wcslen(str->string) + 128;
    neo_js_variable_t error = neo_js_context_create_simple_error(
        vm->ctx, NEO_JS_ERROR_TYPE, len,
        L"Cannot read private member '%ls' from an object whose class did "
        L"not declare it",
        str->string);
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, args, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  size_t argc = num->number;
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_context_get_field(
        vm->ctx, args, neo_js_context_create_number(vm->ctx, idx), NULL);
  }
  neo_js_variable_t callee =
      neo_js_context_get_private(vm->ctx, host, field, NULL);
  neo_js_callable_t callable = neo_js_variable_to_callable(callee);
  neo_js_variable_t result = NULL;
  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  if (callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, callable->name, column,
                                   line);
    result = neo_js_context_call(vm->ctx, callee, host, argc, argv);
    neo_js_context_pop_stackframe(vm->ctx);
  } else if (!callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, NULL, column, line);
    result = neo_js_context_create_simple_error(vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                                L"variable is not callable");
    neo_js_context_pop_stackframe(vm->ctx);
  }
  neo_allocator_free(allocator, argv);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_get_private_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_object_t obj = neo_js_variable_to_object(host);
  if (!obj->constructor || !vm->clazz ||
      neo_js_chunk_get_value(obj->constructor) !=
          neo_js_variable_get_value(vm->clazz)) {
    neo_js_string_t str = neo_js_variable_to_string(field);
    size_t len = wcslen(str->string) + 128;
    neo_js_variable_t error = neo_js_context_create_simple_error(
        vm->ctx, NEO_JS_ERROR_TYPE, len,
        L"Cannot read private member '%ls' from an object whose class did "
        L"not declare it",
        str->string);
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_variable_t value =
      neo_js_context_get_private(vm->ctx, host, field, NULL);
  neo_list_push(vm->stack, value);
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_set_private_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_object_t obj = neo_js_variable_to_object(host);
  if (!obj->constructor || !vm->clazz ||
      neo_js_chunk_get_value(obj->constructor) !=
          neo_js_variable_get_value(vm->clazz)) {
    neo_js_string_t str = neo_js_variable_to_string(field);
    size_t len = wcslen(str->string) + 128;
    neo_js_variable_t error = neo_js_context_create_simple_error(
        vm->ctx, NEO_JS_ERROR_TYPE, len,
        L"Cannot read private member '%ls' from an object whose class did "
        L"not declare it",
        str->string);
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  NEO_VM_CHECK_AND_THROW(
      neo_js_context_set_private(vm->ctx, host, field, value, NULL), vm,
      program);
  if (neo_js_variable_get_type(value)->kind >= NEO_JS_TYPE_CALLABLE &&
      vm->clazz) {
    neo_js_callable_set_class(vm->ctx, value, vm->clazz);
  }
}

void neo_js_vm_set_getter(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t getter = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  host = neo_js_context_to_object(vm->ctx, host);
  NEO_VM_CHECK_AND_THROW(host, vm, program);
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(vm->ctx, host, field);
  neo_js_chunk_t hobject = neo_js_variable_get_chunk(host);
  neo_js_chunk_t hgetter = neo_js_variable_get_chunk(getter);
  neo_js_chunk_add_parent(hgetter, hobject);
  if (prop) {
    if (prop->get) {
      neo_js_chunk_add_parent(
          prop->get,
          neo_js_scope_get_root_chunk(neo_js_context_get_scope(vm->ctx)));
      neo_js_chunk_remove_parent(prop->get, hobject);
      prop->get = NULL;
    }
    prop->get = hgetter;
  } else {
    neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
    prop = neo_create_js_object_property(allocator);
    prop->configurable = true;
    prop->enumerable = true;
    prop->get = hgetter;
    neo_js_object_t obj = neo_js_variable_to_object(host);
    neo_hash_map_set(obj->properties, neo_js_variable_get_chunk(field), prop,
                     vm->ctx, vm->ctx);
  }
}

void neo_js_vm_set_setter(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t setter = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  host = neo_js_context_to_object(vm->ctx, host);
  NEO_VM_CHECK_AND_THROW(host, vm, program);
  neo_js_object_property_t prop =
      neo_js_object_get_own_property(vm->ctx, host, field);
  neo_js_chunk_t hobject = neo_js_variable_get_chunk(host);
  neo_js_chunk_t hsetter = neo_js_variable_get_chunk(setter);
  neo_js_chunk_add_parent(hsetter, hobject);
  if (prop) {
    if (prop->set) {
      neo_js_chunk_add_parent(
          prop->set,
          neo_js_scope_get_root_chunk(neo_js_context_get_scope(vm->ctx)));
      neo_js_chunk_remove_parent(prop->set, hobject);
      prop->set = NULL;
    }
    prop->set = hsetter;
  } else {
    neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
    prop = neo_create_js_object_property(allocator);
    prop->configurable = true;
    prop->enumerable = true;
    prop->set = hsetter;
    neo_js_object_t obj = neo_js_variable_to_object(host);
    neo_hash_map_set(obj->properties, neo_js_variable_get_chunk(field), prop,
                     vm->ctx, vm->ctx);
  }
}

void neo_js_vm_set_method(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  NEO_VM_CHECK_AND_THROW(
      neo_js_context_set_field(vm->ctx, host, field, value, NULL), vm, program);
  if (vm->clazz) {
    NEO_VM_CHECK_AND_THROW(neo_js_callable_set_class(vm->ctx, value, vm->clazz),
                           vm, program);
  }
  wchar_t *name = NULL;
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
    neo_js_variable_t oname = neo_js_context_to_object(vm->ctx, field);
    NEO_VM_CHECK_AND_THROW(oname, vm, program);
    neo_js_variable_t to_string = neo_js_context_get_field(
        vm->ctx, oname, neo_js_context_create_string(vm->ctx, L"toString"),
        NULL);
    neo_js_variable_t vname =
        neo_js_context_call(vm->ctx, to_string, oname, 0, NULL);
    neo_js_string_t sname = neo_js_variable_to_string(vname);
    size_t len = wcslen(sname->string) + 8;
    name = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
    name[0] = 0;
    swprintf(name, len, L"[%ls]", sname->string);
  } else {
    neo_js_variable_t vname = neo_js_context_to_string(vm->ctx, field);
    neo_js_string_t sname = neo_js_variable_to_string(vname);
    size_t len = wcslen(sname->string) + 8;
    name = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
    swprintf(name, len, L"[%ls]", sname->string);
  }
  neo_js_callable_t callable = neo_js_variable_to_callable(value);
  callable->name = name;
}

void neo_js_vm_def_private_getter(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t getter = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_context_def_private_accessor(vm->ctx, host, field, getter, NULL);
}

void neo_js_vm_def_private_setter(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t setter = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_context_def_private_accessor(vm->ctx, host, field, NULL, setter);
}

void neo_js_vm_def_private_method(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t field = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_context_def_private(vm->ctx, host, field, value);
  if (vm->clazz) {
    NEO_VM_CHECK_AND_THROW(neo_js_callable_set_class(vm->ctx, value, vm->clazz),
                           vm, program);
  }
  wchar_t *name = NULL;
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
    neo_js_variable_t oname = neo_js_context_to_object(vm->ctx, field);
    NEO_VM_CHECK_AND_THROW(oname, vm, program);
    neo_js_variable_t to_string = neo_js_context_get_field(
        vm->ctx, oname, neo_js_context_create_string(vm->ctx, L"toString"),
        NULL);
    neo_js_variable_t vname =
        neo_js_context_call(vm->ctx, to_string, oname, 0, NULL);
    neo_js_string_t sname = neo_js_variable_to_string(vname);
    size_t len = wcslen(sname->string) + 8;
    name = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
    name[0] = 0;
    swprintf(name, len, L"[%ls]", sname->string);
  } else {
    neo_js_variable_t vname = neo_js_context_to_string(vm->ctx, field);
    neo_js_string_t sname = neo_js_variable_to_string(vname);
    size_t len = wcslen(sname->string) + 8;
    name = neo_allocator_alloc(allocator, len * sizeof(wchar_t), NULL);
    swprintf(name, len, L"[%ls]", sname->string);
  }
  neo_js_callable_t callable = neo_js_variable_to_callable(value);
  callable->name = name;
}

void neo_js_vm_jnull(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_UNDEFINED ||
      neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_NULL) {
    vm->offset = address;
  }
}

void neo_js_vm_jnot_null(neo_js_vm_t vm, neo_program_t program) {
  size_t address = neo_js_vm_read_address(vm, program);
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(value)->kind != NEO_JS_TYPE_UNDEFINED &&
      neo_js_variable_get_type(value)->kind != NEO_JS_TYPE_NULL) {
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
void neo_js_vm_break(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *label = neo_js_vm_read_string(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_label_frame_t frame = NULL;
  neo_list_node_t it = neo_list_get_last(vm->label_stack);
  while (it != neo_list_get_head(vm->label_stack)) {
    neo_js_label_frame_t f = neo_list_node_get(it);
    if (f->kind == NEO_LABEL_BREAK) {
      if (wcscmp(label, L"") == 0) {
        frame = f;
        break;
      }
      if (wcscmp(label, f->label) == 0) {
        frame = f;
        break;
      }
    }
    neo_allocator_free(allocator, f);
    neo_list_pop(vm->label_stack);
    it = neo_list_get_last(vm->label_stack);
  }
  if (!frame) {
    size_t len = wcslen(label);
    len += 32;
    neo_js_variable_t error = neo_js_context_create_simple_error(
        vm->ctx, NEO_JS_ERROR_SYNTAX, len, L"Undefined label '%ls'", label);
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_list_pop(vm->label_stack);
  neo_js_variable_t interrupt = neo_js_context_create_interrupt(
      vm->ctx, neo_js_context_create_undefined(vm->ctx), 0,
      NEO_JS_INTERRUPT_GOTO);
  neo_js_context_set_opaque(vm->ctx, interrupt, L"#label", frame);
  neo_list_push(vm->stack, interrupt);
  vm->offset = neo_buffer_get_size(program->codes);
}
void neo_js_vm_continue(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *label = neo_js_vm_read_string(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_label_frame_t frame = NULL;
  neo_list_node_t it = neo_list_get_last(vm->label_stack);
  while (it != neo_list_get_head(vm->label_stack)) {
    neo_js_label_frame_t f = neo_list_node_get(it);
    if (f->kind == NEO_LABEL_CONTINUE) {
      if (wcscmp(label, L"") == 0) {
        frame = f;
        break;
      }
      if (wcscmp(label, f->label) == 0) {
        frame = f;
        break;
      }
    }
    neo_allocator_free(allocator, f);
    neo_list_pop(vm->label_stack);
    it = neo_list_get_last(vm->label_stack);
  }
  if (!frame) {
    size_t len = wcslen(label);
    len += 32;
    neo_js_variable_t error = neo_js_context_create_simple_error(
        vm->ctx, NEO_JS_ERROR_SYNTAX, len, L"Undefined label '%ls'", label);
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_list_pop(vm->label_stack);
  neo_js_variable_t interrupt = neo_js_context_create_interrupt(
      vm->ctx, neo_js_context_create_undefined(vm->ctx), 0,
      NEO_JS_INTERRUPT_GOTO);
  neo_js_context_set_opaque(vm->ctx, interrupt, L"#label", frame);
  neo_list_push(vm->stack, interrupt);
  vm->offset = neo_buffer_get_size(program->codes);
}
void neo_js_vm_throw(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t error = neo_js_context_create_error(vm->ctx, value);
  neo_list_push(vm->stack, error);
  vm->offset = neo_buffer_get_size(program->codes);
}

void neo_js_vm_try_begin(neo_js_vm_t vm, neo_program_t program) {
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_try_frame_t frame = neo_create_js_try_frame(allocator);
  frame->scope = neo_js_context_get_scope(vm->ctx);
  frame->label_stack_top = neo_list_get_size(vm->label_stack);
  frame->stack_top = neo_list_get_size(vm->stack);
  frame->onerror = neo_js_vm_read_address(vm, program);
  frame->onfinish = neo_js_vm_read_address(vm, program);
  neo_list_push(vm->try_stack, frame);
}
void neo_js_vm_try_end(neo_js_vm_t vm, neo_program_t program) {
  neo_js_try_frame_t frame =
      neo_list_node_get(neo_list_get_last(vm->try_stack));
  if (frame->onerror == 0 && frame->onfinish == 0) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
  neo_list_pop(vm->try_stack);
}

void neo_js_vm_ret(neo_js_vm_t vm, neo_program_t program) {
  vm->offset = neo_buffer_get_size(program->codes);
}
void neo_js_vm_hlt(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t result = NULL;
  if (vm->result) {
    result = neo_js_context_create_variable(vm->ctx, vm->result, NULL);
  } else {
    result = neo_js_context_create_undefined(vm->ctx);
  }
  neo_list_push(vm->stack, result);
  vm->offset = neo_buffer_get_size(program->codes);
}
void neo_js_vm_keys(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  value = neo_js_context_get_keys(vm->ctx, value);
  neo_list_push(vm->stack, value);
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_await(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t interrupt = neo_js_context_create_interrupt(
      vm->ctx, value, vm->offset, NEO_JS_INTERRUPT_AWAIT);
  neo_list_push(vm->stack, interrupt);
  vm->offset = neo_buffer_get_size(program->codes);
}
void neo_js_vm_yield(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t interrupt = neo_js_context_create_interrupt(
      vm->ctx, value, vm->offset, NEO_JS_INTERRUPT_YIELD);
  neo_list_push(vm->stack, interrupt);
  vm->offset = neo_buffer_get_size(program->codes);
}
void neo_js_vm_iterator(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t iterator = neo_js_context_get_field(
      vm->ctx, neo_js_context_get_std(vm->ctx).symbol_constructor,
      neo_js_context_create_string(vm->ctx, L"iterator"), NULL);
  neo_js_variable_t it =
      neo_js_context_get_field(vm->ctx, value, iterator, NULL);
  if (neo_js_variable_get_type(it)->kind < NEO_JS_TYPE_CALLABLE) {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not iterable"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  } else {
    neo_js_variable_t iterator =
        neo_js_context_call(vm->ctx, it, value, 0, NULL);
    neo_list_push(vm->stack, iterator);
    if (neo_js_variable_get_type(iterator)->kind == NEO_JS_TYPE_ERROR) {
      vm->offset = neo_buffer_get_size(program->codes);
    }
  }
}

void neo_js_vm_async_iterator(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t iterator = neo_js_context_get_field(
      vm->ctx, neo_js_context_get_std(vm->ctx).symbol_constructor,
      neo_js_context_create_string(vm->ctx, L"asyncIterator"), NULL);
  neo_js_variable_t it =
      neo_js_context_get_field(vm->ctx, value, iterator, NULL);
  if (neo_js_variable_get_type(it)->kind >= NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t iterator =
        neo_js_context_call(vm->ctx, it, value, 0, NULL);
    neo_list_push(vm->stack, iterator);
    if (neo_js_variable_get_type(iterator)->kind == NEO_JS_TYPE_ERROR) {
      vm->offset = neo_buffer_get_size(program->codes);
    }
    return;
  }
  neo_js_vm_iterator(vm, program);
}

void neo_js_vm_next(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t iterator = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t next = neo_js_context_get_field(
      vm->ctx, iterator, neo_js_context_create_string(vm->ctx, L"next"), NULL);
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not iterable"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_variable_t result =
      neo_js_context_call(vm->ctx, next, iterator, 0, NULL);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    neo_list_push(vm->stack, result);
    vm->offset = neo_buffer_get_size(program->codes);
  } else {
    neo_js_variable_t value = neo_js_context_get_field(
        vm->ctx, result, neo_js_context_create_string(vm->ctx, L"value"), NULL);
    neo_js_variable_t done = neo_js_context_get_field(
        vm->ctx, result, neo_js_context_create_string(vm->ctx, L"done"), NULL);
    neo_list_push(vm->stack, value);
    neo_list_push(vm->stack, done);
  }
}

void neo_js_vm_await_next(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t iterator = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t next = neo_js_context_get_field(
      vm->ctx, iterator, neo_js_context_create_string(vm->ctx, L"next"), NULL);
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not iterable"));
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  neo_js_variable_t result =
      neo_js_context_call(vm->ctx, next, iterator, 0, NULL);
  if (neo_js_variable_get_type(result)->kind != NEO_JS_TYPE_ERROR) {
    result = neo_js_context_create_interrupt(vm->ctx, result, vm->offset,
                                             NEO_JS_INTERRUPT_AWAIT);
  }
  neo_list_push(vm->stack, result);
  vm->offset = neo_buffer_get_size(program->codes);
}
void neo_js_vm_resolve_next(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t result = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t value = neo_js_context_get_field(
      vm->ctx, result, neo_js_context_create_string(vm->ctx, L"value"), NULL);
  neo_js_variable_t done = neo_js_context_get_field(
      vm->ctx, result, neo_js_context_create_string(vm->ctx, L"done"), NULL);
  neo_list_push(vm->stack, value);
  neo_list_push(vm->stack, done);
}

void neo_js_vm_rest(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t iterator = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_js_variable_t next = neo_js_context_get_field(
      vm->ctx, iterator, neo_js_context_create_string(vm->ctx, L"next"), NULL);
  if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not iterable"));
    vm->offset = neo_buffer_get_size(program->codes);
  }
  neo_js_variable_t result = NULL;
  neo_js_variable_t array = neo_js_context_create_array(vm->ctx);
  int64_t idx = 0;
  for (;;) {
    result = neo_js_context_call(vm->ctx, next, iterator, 0, NULL);
    NEO_VM_CHECK_AND_THROW(result, vm, program);
    neo_js_variable_t value = neo_js_context_get_field(
        vm->ctx, result, neo_js_context_create_string(vm->ctx, L"value"), NULL);
    neo_js_variable_t done = neo_js_context_get_field(
        vm->ctx, result, neo_js_context_create_string(vm->ctx, L"done"), NULL);
    done = neo_js_context_to_boolean(vm->ctx, done);
    neo_js_boolean_t boolean = neo_js_variable_to_boolean(done);
    if (boolean->boolean) {
      break;
    } else {
      neo_js_context_set_field(vm->ctx, array,
                               neo_js_context_create_number(vm->ctx, idx),
                               value, NULL);
      idx++;
    }
  }
  neo_list_push(vm->stack, array);
}

void neo_js_vm_rest_object(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t keys = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t object = neo_list_node_get(neo_list_get_last(vm->stack));
  object = neo_js_context_to_object(vm->ctx, object);
  NEO_VM_CHECK_AND_THROW(object, vm, program);
  neo_list_t field_keys = neo_js_object_get_keys(vm->ctx, object);
  neo_list_t symbol_keys = neo_js_object_get_own_symbol_keys(vm->ctx, object);

  neo_js_variable_t result = neo_js_context_create_object(vm->ctx, NULL);
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, keys, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  for (int64_t idx = 0; idx < num->number; idx++) {
    neo_js_variable_t key = neo_js_context_get_field(
        vm->ctx, keys, neo_js_context_create_number(vm->ctx, idx), NULL);
    neo_list_t okeys = field_keys;
    if (neo_js_variable_get_type(key)->kind == NEO_JS_TYPE_SYMBOL) {
      okeys = symbol_keys;
    }
    for (neo_list_node_t it = neo_list_get_first(okeys);
         it != neo_list_get_tail(okeys); it = neo_list_node_next(it)) {
      neo_js_variable_t current = neo_list_node_get(it);
      neo_js_variable_t result = neo_js_context_is_equal(vm->ctx, current, key);
      neo_js_boolean_t boolean = neo_js_variable_to_boolean(result);
      if (boolean->boolean) {
        neo_list_erase(okeys, it);
        break;
      }
    }
  }
  for (neo_list_node_t it = neo_list_get_first(field_keys);
       it != neo_list_get_tail(field_keys); it = neo_list_node_next(it)) {
    neo_js_variable_t key = neo_list_node_get(it);
    neo_js_context_set_field(
        vm->ctx, result, key,
        neo_js_context_get_field(vm->ctx, object, key, NULL), NULL);
  }
  for (neo_list_node_t it = neo_list_get_first(symbol_keys);
       it != neo_list_get_tail(symbol_keys); it = neo_list_node_next(it)) {
    neo_js_variable_t key = neo_list_node_get(it);
    neo_js_context_set_field(
        vm->ctx, result, key,
        neo_js_context_get_field(vm->ctx, object, key, NULL), NULL);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_allocator_free(allocator, field_keys);
  neo_allocator_free(allocator, symbol_keys);
  neo_list_push(vm->stack, result);
}

static char *neo_js_vm_resolve_module_name(neo_js_vm_t vm,
                                           neo_program_t program,
                                           const char *path) {
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  wchar_t *wpath = neo_string_to_wstring(allocator, path);
  neo_js_context_defer_free(vm->ctx, wpath);
  if (neo_js_context_has_module(vm->ctx, wpath)) {
    return neo_create_string(allocator, path);
  }
  neo_path_t current = neo_create_path(allocator, program->dirname);
  neo_path_t file = neo_create_path(allocator, path);
  neo_path_t total = neo_path_concat(current, file);
  neo_allocator_free(allocator, file);
  neo_allocator_free(allocator, current);
  current = total;
  char *fp = neo_path_to_string(current);
  if (neo_fs_exist(allocator, fp)) {
    if (!neo_fs_is_dir(allocator, fp)) {
      neo_allocator_free(allocator, current);
      return fp;
    }
    neo_allocator_free(allocator, fp);
    file = neo_path_clone(current);
    neo_path_append(file, neo_create_string(allocator, "index.js"));
    fp = neo_path_to_string(file);
    if (neo_fs_exist(allocator, fp)) {
      neo_allocator_free(allocator, current);
      neo_allocator_free(allocator, file);
      return fp;
    }
    neo_allocator_free(allocator, fp);
    file = neo_path_clone(current);
    neo_path_append(file, neo_create_string(allocator, "index.mjs"));
    fp = neo_path_to_string(file);
    if (neo_fs_exist(allocator, fp)) {
      neo_allocator_free(allocator, current);
      neo_allocator_free(allocator, file);
      return fp;
    }
    neo_allocator_free(allocator, fp);
    file = neo_path_clone(current);
    neo_path_append(file, neo_create_string(allocator, "index.json"));
    fp = neo_path_to_string(file);
    if (neo_fs_exist(allocator, fp)) {
      neo_allocator_free(allocator, current);
      neo_allocator_free(allocator, file);
      return fp;
    }
  } else {
    neo_allocator_free(allocator, current);
    size_t max = strlen(fp) + 1;
    char *full = neo_create_string(allocator, fp);
    full = neo_string_concat(allocator, full, &max, ".js");
    if (neo_fs_exist(allocator, full) && !neo_fs_is_dir(allocator, full)) {
      neo_allocator_free(allocator, fp);
      return full;
    }
    neo_allocator_free(allocator, full);
    max = strlen(fp) + 1;
    full = neo_create_string(allocator, fp);
    full = neo_string_concat(allocator, full, &max, ".mjs");
    if (neo_fs_exist(allocator, full) && !neo_fs_is_dir(allocator, full)) {
      neo_allocator_free(allocator, fp);
      return full;
    }
    neo_allocator_free(allocator, full);
    max = strlen(fp) + 1;
    full = neo_create_string(allocator, fp);
    full = neo_string_concat(allocator, full, &max, ".json");
    if (neo_fs_exist(allocator, full) && !neo_fs_is_dir(allocator, full)) {
      neo_allocator_free(allocator, fp);
      return full;
    }
    neo_allocator_free(allocator, full);
  }
  neo_allocator_free(allocator, fp);
  neo_allocator_free(allocator, current);
  return NULL;
}

void neo_js_vm_import(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  char *sname = neo_wstring_to_string(allocator, name);
  char *m_name = neo_js_vm_resolve_module_name(vm, program, sname);
  neo_js_context_defer_free(vm->ctx, m_name);
  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  if (!m_name) {
    size_t len = wcslen(name) + 64 + wcslen(filename);
    neo_js_variable_t error = neo_js_context_create_simple_error(
        vm->ctx, NEO_JS_ERROR_SYNTAX, len,
        L"Cannot find module '%ls' imported from %ls", name, filename);
    neo_list_push(vm->stack, error);
    vm->offset = neo_buffer_get_size(program->codes);
    return;
  }
  wchar_t *module_name = neo_string_to_wstring(allocator, m_name);
  neo_js_context_defer_free(vm->ctx, module_name);
  if (!neo_js_context_has_module(vm->ctx, module_name)) {
    char *source = neo_fs_read_file(allocator, m_name);
    char *m_name = neo_wstring_to_string(allocator, module_name);
    neo_js_variable_t error = neo_js_context_eval(vm->ctx, m_name, source);
    neo_allocator_free(allocator, m_name);
    neo_allocator_free(allocator, source);
    if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
      neo_list_push(vm->stack, error);
      vm->offset = neo_buffer_get_size(program->codes);
      neo_allocator_free(allocator, module_name);
      return;
    }
  }
  neo_js_variable_t module = neo_js_context_get_module(vm->ctx, module_name);
  neo_list_push(vm->stack, module);
  if (neo_js_variable_get_type(module)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
  neo_allocator_free(allocator, module_name);
}
void neo_js_vm_assert(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *type = neo_js_vm_read_string(vm, program);
  const wchar_t *value = neo_js_vm_read_string(vm, program);
  const wchar_t *path = neo_js_vm_read_string(vm, program);
  NEO_VM_CHECK_AND_THROW(neo_js_context_assert(vm->ctx, type, value, path), vm,
                         program);
}
void neo_js_vm_export(neo_js_vm_t vm, neo_program_t program) {
  const wchar_t *name = neo_js_vm_read_string(vm, program);
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  neo_js_variable_t module = neo_js_context_get_module(vm->ctx, filename);
  neo_js_context_set_field(vm->ctx, module,
                           neo_js_context_create_string(vm->ctx, name), value,
                           NULL);
}
void neo_js_vm_export_all(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t keys = neo_js_context_get_keys(vm->ctx, value);
  NEO_VM_CHECK_AND_THROW(keys, vm, program);
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, keys, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  neo_js_variable_t module = neo_js_context_get_module(vm->ctx, filename);
  for (size_t idx = 0; idx < num->number; idx++) {
    neo_js_variable_t field = neo_js_context_get_field(
        vm->ctx, keys, neo_js_context_create_number(vm->ctx, idx), NULL);
    neo_js_variable_t val =
        neo_js_context_get_field(vm->ctx, value, field, NULL);
    neo_js_context_set_field(vm->ctx, module, field, val, NULL);
  }
}

void neo_js_vm_new(neo_js_vm_t vm, neo_program_t program) {
  uint32_t line = neo_js_vm_read_integer(vm, program);
  uint32_t column = neo_js_vm_read_integer(vm, program);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  neo_js_variable_t args = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t length = neo_js_context_get_field(
      vm->ctx, args, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  neo_js_number_t num = neo_js_variable_to_number(length);
  size_t argc = num->number;
  neo_js_variable_t *argv =
      neo_allocator_alloc(allocator, sizeof(neo_js_variable_t) * argc, NULL);
  for (size_t idx = 0; idx < argc; idx++) {
    argv[idx] = neo_js_context_get_field(
        vm->ctx, args, neo_js_context_create_number(vm->ctx, idx), NULL);
  }
  neo_js_variable_t callee = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_callable_t callable = neo_js_variable_to_callable(callee);
  neo_js_variable_t result = NULL;
  wchar_t *filename = neo_string_to_wstring(allocator, program->filename);
  neo_js_context_defer_free(vm->ctx, filename);
  if (!callable) {
    neo_js_context_push_stackframe(vm->ctx, filename, NULL, column, line);
    result = neo_js_context_create_simple_error(
        vm->ctx, NEO_JS_ERROR_TYPE, 0, L"variable is not constructable");
    neo_js_context_pop_stackframe(vm->ctx);
  } else {
    neo_js_context_push_stackframe(vm->ctx, filename, callable->name, column,
                                   line);
    result = neo_js_context_construct(vm->ctx, callee, argc, argv);
    neo_js_context_pop_stackframe(vm->ctx);
  }
  neo_allocator_free(allocator, argv);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_eq(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_is_equal(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_ne(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_is_equal(vm->ctx, left, right);
  if (neo_js_variable_get_type(result)->kind != NEO_JS_TYPE_ERROR) {
    neo_js_boolean_t boolean = neo_js_variable_to_boolean(result);
    result = neo_js_context_create_boolean(vm->ctx, !boolean->boolean);
  }
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_seq(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  bool is_equal =
      neo_js_variable_get_type(left) == neo_js_variable_get_type(right);
  neo_js_variable_t result = NULL;
  if (is_equal) {
    neo_js_variable_t result = neo_js_context_is_equal(vm->ctx, left, right);
    if (neo_js_variable_get_type(result)->kind != NEO_JS_TYPE_ERROR) {
      neo_js_boolean_t boolean = neo_js_variable_to_boolean(result);
      is_equal &= boolean->boolean;
    }
  }
  if (!result || neo_js_variable_get_type(result)->kind != NEO_JS_TYPE_ERROR) {
    result = neo_js_context_create_boolean(vm->ctx, is_equal);
  }
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_sne(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  bool is_equal =
      neo_js_variable_get_type(left) == neo_js_variable_get_type(right);
  neo_js_variable_t result = NULL;
  if (is_equal) {
    neo_js_variable_t result = neo_js_context_is_equal(vm->ctx, left, right);
    if (neo_js_variable_get_type(result)->kind != NEO_JS_TYPE_ERROR) {
      neo_js_boolean_t boolean = neo_js_variable_to_boolean(result);
      is_equal |= boolean->boolean;
    }
  }
  if (!result || neo_js_variable_get_type(result)->kind != NEO_JS_TYPE_ERROR) {
    result = neo_js_context_create_boolean(vm->ctx, !is_equal);
  }
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_del(neo_js_vm_t vm, neo_program_t program) {
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_context_create_boolean(vm->ctx, true);
  neo_list_push(vm->stack, res);
}

void neo_js_vm_gt(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_is_gt(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_lt(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_is_lt(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_ge(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_is_ge(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_le(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_is_le(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_typeof(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_typeof(vm->ctx, value);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_void(neo_js_vm_t vm, neo_program_t program) {
  neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_list_push(vm->stack, neo_js_context_create_undefined(vm->ctx));
}
void neo_js_vm_inc(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  value = neo_js_context_inc(vm->ctx, value);
  neo_list_push(vm->stack, value);
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_dec(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  value = neo_js_context_dec(vm->ctx, value);
  neo_list_push(vm->stack, value);
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_add(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_add(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}

void neo_js_vm_sub(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_sub(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_mul(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_mul(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_div(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_div(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_mod(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_mod(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_pow(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_pow(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_not(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_not(vm->ctx, variable);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_and(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_and(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_or(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_or(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_xor(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_xor(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_shr(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_shr(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_shl(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_shl(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_ushr(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_ushr(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_plus(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_plus(vm->ctx, value);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_neg(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_neg(vm->ctx, value);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_logical_not(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t variable = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_logical_not(vm->ctx, variable);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_concat(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t result = neo_js_context_concat(vm->ctx, left, right);
  neo_list_push(vm->stack, result);
  if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_spread(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t value = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t host = neo_list_node_get(neo_list_get_last(vm->stack));
  if (neo_js_variable_get_type(host)->kind == NEO_JS_TYPE_ARRAY) {
    neo_js_variable_t length = neo_js_context_get_field(
        vm->ctx, host, neo_js_context_create_string(vm->ctx, L"length"), NULL);
    NEO_VM_CHECK_AND_THROW(length, vm, program);
    neo_js_number_t nlength = neo_js_variable_to_number(length);
    if (!nlength) {
      neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                   vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                   L"variable is not array-link"));
      vm->offset = neo_buffer_get_size(program->codes);
      return;
    }
    neo_js_variable_t iterator = neo_js_context_get_field(
        vm->ctx, neo_js_context_get_std(vm->ctx).symbol_constructor,
        neo_js_context_create_string(vm->ctx, L"iterator"), NULL);
    iterator = neo_js_context_get_field(vm->ctx, value, iterator, NULL);
    NEO_VM_CHECK_AND_THROW(iterator, vm, program);
    if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
      neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                   vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                   L"variable is not iterable"));
      vm->offset = neo_buffer_get_size(program->codes);
      return;
    }
    iterator = neo_js_context_call(vm->ctx, iterator, value, 0, NULL);
    NEO_VM_CHECK_AND_THROW(iterator, vm, program);
    neo_js_variable_t next = neo_js_context_get_field(
        vm->ctx, iterator, neo_js_context_create_string(vm->ctx, L"next"),
        NULL);
    NEO_VM_CHECK_AND_THROW(next, vm, program);
    if (neo_js_variable_get_type(next)->kind < NEO_JS_TYPE_CALLABLE) {
      neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                   vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                   L"variable is not iterable"));
      vm->offset = neo_buffer_get_size(program->codes);
      return;
    }
    neo_js_variable_t result =
        neo_js_context_call(vm->ctx, next, iterator, 0, NULL);
    NEO_VM_CHECK_AND_THROW(result, vm, program);
    neo_js_variable_t done = neo_js_context_get_field(
        vm->ctx, result, neo_js_context_create_string(vm->ctx, L"done"), NULL);
    neo_js_variable_t value = neo_js_context_get_field(
        vm->ctx, result, neo_js_context_create_string(vm->ctx, L"value"), NULL);
    done = neo_js_context_to_boolean(vm->ctx, done);
    NEO_VM_CHECK_AND_THROW(done, vm, program);
    neo_js_boolean_t boolean = neo_js_variable_to_boolean(done);
    int64_t idx = nlength->number;
    while (!boolean->boolean) {
      neo_js_context_set_field(vm->ctx, host,
                               neo_js_context_create_number(vm->ctx, idx),
                               value, NULL);
      idx++;
      result = neo_js_context_call(vm->ctx, next, iterator, 0, NULL);
      NEO_VM_CHECK_AND_THROW(result, vm, program);
      done = neo_js_context_get_field(
          vm->ctx, result, neo_js_context_create_string(vm->ctx, L"done"),
          NULL);
      value = neo_js_context_get_field(
          vm->ctx, result, neo_js_context_create_string(vm->ctx, L"value"),
          NULL);
      done = neo_js_context_to_boolean(vm->ctx, done);
      NEO_VM_CHECK_AND_THROW(done, vm, program);
      boolean = neo_js_variable_to_boolean(done);
    }
  } else if (neo_js_variable_get_type(host)->kind == NEO_JS_TYPE_OBJECT) {
    value = neo_js_context_to_object(vm->ctx, value);
    neo_list_t keys = neo_js_object_get_keys(vm->ctx, value);
    neo_list_t symbol_keys = neo_js_object_get_own_symbol_keys(vm->ctx, value);
    for (neo_list_node_t it = neo_list_get_first(keys);
         it != neo_list_get_tail(keys); it = neo_list_node_next(it)) {
      neo_js_variable_t key = neo_list_node_get(it);
      neo_js_context_set_field(
          vm->ctx, host, key,
          neo_js_context_get_field(vm->ctx, value, key, NULL), NULL);
    }
    for (neo_list_node_t it = neo_list_get_first(symbol_keys);
         it != neo_list_get_tail(symbol_keys); it = neo_list_node_next(it)) {
      neo_js_variable_t key = neo_list_node_get(it);
      neo_js_context_set_field(
          vm->ctx, host, key,
          neo_js_context_get_field(vm->ctx, value, key, NULL), NULL);
    }
  } else {
    neo_list_push(vm->stack, neo_js_context_create_simple_error(
                                 vm->ctx, NEO_JS_ERROR_TYPE, 0,
                                 L"variable is not a object/array"));
    vm->offset = neo_buffer_get_size(program->codes);
  }
}
void neo_js_vm_in(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_context_in(vm->ctx, left, right);
  neo_list_push(vm->stack, res);
}
void neo_js_vm_instanceof(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t right = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t left = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t res = neo_js_context_instance_of(vm->ctx, left, right);
  neo_list_push(vm->stack, res);
}
void neo_js_vm_tag(neo_js_vm_t vm, neo_program_t program) {
  neo_list_node_t it = neo_list_get_last(vm->stack);
  neo_js_variable_t arguments = neo_list_node_get(it);
  it = neo_list_node_last(it);
  neo_js_variable_t callee = neo_list_node_get(it);
  if (neo_js_variable_get_type(callee)->kind == NEO_JS_TYPE_CFUNCTION &&
      (neo_js_variable_to_cfunction(callee)->callee == neo_js_string_raw)) {
    return neo_js_vm_call(vm, program);
  }
  neo_js_variable_t qusis = neo_js_context_get_field(
      vm->ctx, arguments, neo_js_context_create_string(vm->ctx, L"0"), NULL);
  neo_js_variable_t vlength = neo_js_context_get_field(
      vm->ctx, qusis, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  int64_t length = neo_js_variable_to_number(vlength)->number;
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  for (int64_t idx = 0; idx < length; idx++) {
    neo_js_variable_t item = neo_js_context_get_field(
        vm->ctx, qusis, neo_js_context_create_number(vm->ctx, idx), NULL);
    neo_js_string_t str = neo_js_variable_to_string(item);
    wchar_t *decode_string = neo_wstring_decode(allocator, str->string);
    neo_allocator_free(allocator, str->string);
    str->string = decode_string;
  }
  neo_js_vm_call(vm, program);
}

void neo_js_vm_member_tag(neo_js_vm_t vm, neo_program_t program) {
  neo_list_node_t it = neo_list_get_last(vm->stack);
  neo_js_variable_t arguments = neo_list_node_get(it);
  it = neo_list_node_last(it);
  neo_js_variable_t field = neo_list_node_get(it);
  it = neo_list_node_last(it);
  neo_js_variable_t host = neo_list_node_get(it);
  neo_js_variable_t method =
      neo_js_context_get_field(vm->ctx, host, field, NULL);
  NEO_VM_CHECK_AND_THROW(method, vm, program);
  if (neo_js_variable_get_type(method)->kind == NEO_JS_TYPE_CFUNCTION &&
      neo_js_variable_to_cfunction(method)->callee == neo_js_string_raw) {
    return neo_js_vm_member_call(vm, program);
  }
  neo_js_variable_t qusis = neo_js_context_get_field(
      vm->ctx, arguments, neo_js_context_create_string(vm->ctx, L"0"), NULL);
  neo_js_variable_t vlength = neo_js_context_get_field(
      vm->ctx, qusis, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  int64_t length = neo_js_variable_to_number(vlength)->number;
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  for (int64_t idx = 0; idx < length; idx++) {
    neo_js_variable_t item = neo_js_context_get_field(
        vm->ctx, qusis, neo_js_context_create_number(vm->ctx, idx), NULL);
    neo_js_string_t str = neo_js_variable_to_string(item);
    wchar_t *decode_string = neo_wstring_decode(allocator, str->string);
    neo_allocator_free(allocator, str->string);
    str->string = decode_string;
  }
  neo_js_vm_member_call(vm, program);
}

void neo_js_vm_private_tag(neo_js_vm_t vm, neo_program_t program) {
  neo_list_node_t it = neo_list_get_last(vm->stack);
  neo_js_variable_t arguments = neo_list_node_get(it);
  it = neo_list_node_last(it);
  neo_js_variable_t field = neo_list_node_get(it);
  it = neo_list_node_last(it);
  neo_js_variable_t host = neo_list_node_get(it);
  neo_js_variable_t method =
      neo_js_context_get_private(vm->ctx, host, field, NULL);
  NEO_VM_CHECK_AND_THROW(method, vm, program);
  if (neo_js_variable_get_type(method)->kind == NEO_JS_TYPE_CFUNCTION &&
      neo_js_variable_to_cfunction(method)->callee == neo_js_string_raw) {
    return neo_js_vm_private_member_call(vm, program);
  }
  neo_js_variable_t qusis = neo_js_context_get_field(
      vm->ctx, arguments, neo_js_context_create_string(vm->ctx, L"0"), NULL);
  neo_js_variable_t vlength = neo_js_context_get_field(
      vm->ctx, qusis, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  int64_t length = neo_js_variable_to_number(vlength)->number;
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  for (int64_t idx = 0; idx < length; idx++) {
    neo_js_variable_t item = neo_js_context_get_field(
        vm->ctx, qusis, neo_js_context_create_number(vm->ctx, idx), NULL);
    neo_js_string_t str = neo_js_variable_to_string(item);
    wchar_t *decode_string = neo_wstring_decode(allocator, str->string);
    neo_allocator_free(allocator, str->string);
    str->string = decode_string;
  }
  neo_js_vm_private_member_call(vm, program);
}

void neo_js_vm_super_member_tag(neo_js_vm_t vm, neo_program_t program) {
  neo_list_node_t it = neo_list_get_last(vm->stack);
  neo_js_variable_t arguments = neo_list_node_get(it);
  it = neo_list_node_last(it);
  neo_js_variable_t field = neo_list_node_get(it);
  neo_js_variable_t host = NULL;
  if (neo_js_variable_get_type(vm->self)->kind >= NEO_JS_TYPE_CALLABLE) {
    host = neo_js_object_get_prototype(vm->ctx, vm->self);
  } else {
    host = neo_js_object_get_prototype(vm->ctx, vm->self);
    host = neo_js_object_get_prototype(vm->ctx, host);
  }
  neo_js_variable_t method =
      neo_js_context_get_private(vm->ctx, host, field, NULL);
  NEO_VM_CHECK_AND_THROW(method, vm, program);
  if (neo_js_variable_get_type(method)->kind == NEO_JS_TYPE_CFUNCTION &&
      neo_js_variable_to_cfunction(method)->callee == neo_js_string_raw) {
    return neo_js_vm_private_member_call(vm, program);
  }
  neo_js_variable_t qusis = neo_js_context_get_field(
      vm->ctx, arguments, neo_js_context_create_string(vm->ctx, L"0"), NULL);
  neo_js_variable_t vlength = neo_js_context_get_field(
      vm->ctx, qusis, neo_js_context_create_string(vm->ctx, L"length"), NULL);
  int64_t length = neo_js_variable_to_number(vlength)->number;
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  for (int64_t idx = 0; idx < length; idx++) {
    neo_js_variable_t item = neo_js_context_get_field(
        vm->ctx, qusis, neo_js_context_create_number(vm->ctx, idx), NULL);
    neo_js_string_t str = neo_js_variable_to_string(item);
    wchar_t *decode_string = neo_wstring_decode(allocator, str->string);
    neo_allocator_free(allocator, str->string);
    str->string = decode_string;
  }
  neo_js_vm_super_member_call(vm, program);
}

void neo_js_vm_del_field(neo_js_vm_t vm, neo_program_t program) {
  neo_js_variable_t key = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  neo_js_variable_t object = neo_list_node_get(neo_list_get_last(vm->stack));
  neo_list_pop(vm->stack);
  NEO_VM_CHECK_AND_THROW(neo_js_context_del_field(vm->ctx, object, key), vm,
                         program);
}

const neo_js_vm_cmd_fn_t cmds[] = {
    neo_js_vm_push_scope,            // NEO_ASM_PUSH_SCOPE
    neo_js_vm_pop_scope,             // NEO_ASM_POP_SCOPE
    neo_js_vm_pop,                   // NEO_ASM_POP
    neo_js_vm_store,                 // NEO_ASM_STORE
    neo_js_vm_save,                  // NEO_ASM_SAVE
    neo_js_vm_def,                   // NEO_ASM_DEF
    neo_js_vm_load,                  // NEO_ASM_LOAD
    neo_js_vm_clone,                 // NEO_ASM_CLONE
    neo_js_vm_init_accessor,         // NEO_ASM_INIT_ACCESSOR
    neo_js_vm_init_private_accessor, // NEO_ASM_INIT_PRIVATE_ACCESSOR
    neo_js_vm_init_field,            // NEO_ASM_INIT_FIELD
    neo_js_vm_init_private_field,    // NEO_ASM_INIT_PRIVATE_FIELD
    neo_js_vm_push_undefined,        // NEO_ASM_PUSH_UNDEFINED
    neo_js_vm_push_null,             // NEO_ASM_PUSH_NULL
    neo_js_vm_push_nan,              // NEO_ASM_PUSH_NAN
    neo_js_vm_push_infinity,         // NEO_ASM_PUSH_INFINTY
    neo_js_vm_push_uninitialize,     // NEO_ASM_PUSH_UNINITIALIZED
    neo_js_vm_push_true,             // NEO_ASM_PUSH_TRUE
    neo_js_vm_push_false,            // NEO_ASM_PUSH_FALSE
    neo_js_vm_push_number,           // NEO_ASM_PUSH_NUMBER
    neo_js_vm_push_string,           // NEO_ASM_PUSH_STRING
    neo_js_vm_push_bigint,           // NEO_ASM_PUSH_BIGINT
    neo_js_vm_push_regexp,           // NEO_ASM_PUSH_REGEXP
    neo_js_vm_push_function,         // NEO_ASM_PUSH_FUNCTION
    neo_js_vm_push_class,            // NEO_ASM_PUSH_CLASS
    neo_js_vm_push_async_function,   // NEO_ASM_PUSH_ASYNC_FUNCTION
    neo_js_vm_push_lambda,           // NEO_ASM_PUSH_LAMBDA
    neo_js_vm_push_async_lambda,     // NEO_ASM_PUSH_ASYNC_LAMBDA
    neo_js_vm_push_generator,        // NEO_ASM_PUSH_GENERATOR
    neo_js_vm_push_async_generator,  // NEO_ASM_PUSH_ASYNC_GENERATOR
    neo_js_vm_push_object,           // NEO_ASM_PUSH_OBJECT
    neo_js_vm_push_array,            // NEO_ASM_PUSH_ARRAY
    neo_js_vm_push_this,             // NEO_ASM_PUSH_THIS
    neo_js_vm_super_call,            // NEO_ASM_SUPER_CALL
    neo_js_vm_super_member_call,     // NEO_ASM_SUPER_MEMBER_CALL
    neo_js_vm_get_super_member,      // NEO_ASM_GET_SUPER_FIELD
    neo_js_vm_set_field,             // NEO_ASM_SET_SUPER_FIELD
    neo_js_vm_push_value,            // NEO_ASM_PUSH_VALUE
    neo_js_vm_push_break_label,      // NEO_ASM_PUSH_BREAK_LABEL
    neo_js_vm_push_continue_label,   // NEO_ASM_PUSH_CONTINUE_LABEL
    neo_js_vm_pop_label,             // NEO_ASM_POP_LABEL
    neo_js_vm_set_const,             // NEO_ASM_SET_CONST
    neo_js_vm_set_using,             // NEO_ASM_SET_USING
    neo_js_vm_set_await_using,       // NEO_ASM_SET_AWAIT_USING
    neo_js_vm_set_source,            // NEO_ASM_SET_SOURCE
    neo_js_vm_set_bind,              // NEO_ASM_SET_BIND
    neo_js_vm_set_class,             // NEO_ASM_SET_CLASS
    neo_js_vm_set_address,           // NEO_ASM_SET_ADDRESS
    neo_js_vm_set_name,              // NEO_ASM_SET_NAME
    neo_js_vm_set_closure,           // NEO_ASM_SET_CLOSURE
    neo_js_vm_extends,               // NEO_ASM_EXTENDS
    NULL,                            // NEO_ASM_DECORATOR
    neo_js_vm_direcitve,             // NEO_ASM_DIRECTIVE
    neo_js_vm_call,                  // NEO_ASM_CALL
    neo_js_vm_eval,                  // NEO_ASM_EVAL
    neo_js_vm_member_call,           // NEO_ASM_MEMBER_CALL
    neo_js_vm_get_field,             // NEO_ASM_GET_FIELD
    neo_js_vm_set_field,             // NEO_ASM_SET_FIELD
    neo_js_vm_private_member_call,   // NEO_ASM_PRIVATE_CALL
    neo_js_vm_get_private_field,     // NEO_ASM_GET_PRIVATE_FIELD
    neo_js_vm_set_private_field,     // NEO_ASM_SET_PRIVATE_FIELD
    neo_js_vm_set_getter,            // NEO_ASM_SET_GETTER
    neo_js_vm_set_setter,            // NEO_ASM_SET_SETTER
    neo_js_vm_set_method,            // NEO_ASM_SET_METHOD
    neo_js_vm_def_private_getter,    // NEO_ASM_DEF_PRIVATE_GETTER
    neo_js_vm_def_private_setter,    // NEO_ASM_DEF_PRIVATE_SETTER
    neo_js_vm_def_private_method,    // NEO_ASM_DEF_PRIVATE_METHOD
    neo_js_vm_jnull,                 // NEO_ASM_JNULL
    neo_js_vm_jnot_null,             // NEO_ASM_JNOT_NULL
    neo_js_vm_jfalse,                // NEO_ASM_JFALSE
    neo_js_vm_jtrue,                 // NEO_ASM_JTRUE
    neo_js_vm_jmp,                   // NEO_ASM_JMP
    neo_js_vm_break,                 // NEO_ASM_BREAK
    neo_js_vm_continue,              // NEO_ASM_CONTINUE
    neo_js_vm_throw,                 // NEO_ASM_THROW
    neo_js_vm_try_begin,             // NEO_ASM_TRY_BEGIN
    neo_js_vm_try_end,               // NEO_ASM_TRY_END
    neo_js_vm_ret,                   // NEO_ASM_RET
    neo_js_vm_hlt,                   // NEO_ASM_HLT
    neo_js_vm_keys,                  // NEO_ASM_KEYS
    neo_js_vm_await,                 // NEO_ASM_AWAIT
    neo_js_vm_yield,                 // NEO_ASM_YIELD
    neo_js_vm_next,                  // NEO_ASM_NEXT
    neo_js_vm_await_next,            // NEO_ASM_AWAIT_NEXT
    neo_js_vm_resolve_next,          // NEO_ASM_RESOLVE_NEXT
    neo_js_vm_iterator,              // NEO_ASM_ITERATOR
    neo_js_vm_async_iterator,        // NEO_ASM_ASYNC_ITERATOR
    neo_js_vm_rest,                  // NEO_ASM_REST
    neo_js_vm_rest_object,           // NEO_ASM_REST_OBJECT
    neo_js_vm_import,                // NEO_ASM_IMPORT
    neo_js_vm_assert,                // NEO_ASM_ASSERT
    neo_js_vm_export,                // NEO_ASM_EXPORT
    neo_js_vm_export_all,            // NEO_ASM_EXPORT_ALL
    NULL,                            // NEO_ASM_BREAKPOINT
    neo_js_vm_new,                   // NEO_ASM_NEW
    neo_js_vm_eq,                    // NEO_ASM_EQ
    neo_js_vm_ne,                    // NEO_ASM_NE
    neo_js_vm_seq,                   // NEO_ASM_SEQ
    neo_js_vm_gt,                    // NEO_ASM_GT
    neo_js_vm_lt,                    // NEO_ASM_LT
    neo_js_vm_ge,                    // NEO_ASM_GE
    neo_js_vm_le,                    // NEO_ASM_LE
    neo_js_vm_sne,                   // NEO_ASM_SNE
    neo_js_vm_del,                   // NEO_ASM_DEL
    neo_js_vm_typeof,                // NEO_ASM_TYPEOF
    neo_js_vm_void,                  // NEO_ASM_VOID
    neo_js_vm_inc,                   // NEO_ASM_INC
    neo_js_vm_dec,                   // NEO_ASM_DEC
    neo_js_vm_add,                   // NEO_ASM_ADD
    neo_js_vm_sub,                   // NEO_ASM_SUB
    neo_js_vm_mul,                   // NEO_ASM_MUL
    neo_js_vm_div,                   // NEO_ASM_DIV
    neo_js_vm_mod,                   // NEO_ASM_MOD
    neo_js_vm_pow,                   // NEO_ASM_POW
    neo_js_vm_not,                   // NEO_ASM_NOT
    neo_js_vm_and,                   // NEO_ASM_AND
    neo_js_vm_or,                    // NEO_ASM_OR
    neo_js_vm_xor,                   // NEO_ASM_XOR
    neo_js_vm_shr,                   // NEO_ASM_SHR
    neo_js_vm_shl,                   // NEO_ASM_SHL
    neo_js_vm_ushr,                  // NEO_ASM_USHR
    neo_js_vm_plus,                  // NEO_ASM_PLUS
    neo_js_vm_neg,                   // NEO_ASM_NEG
    neo_js_vm_logical_not,           // NEO_ASM_LOGICAL_NOT
    neo_js_vm_concat,                // NEO_ASM_CONCAT
    neo_js_vm_spread,                // NEO_ASM_SPREAD
    neo_js_vm_in,                    // NEO_ASM_IN
    neo_js_vm_instanceof,            // NEO_ASM_INSTANCE_OF
    neo_js_vm_tag,                   // NEO_ASM_TAG
    neo_js_vm_member_tag,            // NEO_ASM_MEMBER_TAG
    neo_js_vm_private_tag,           // NEO_ASM_PRIVATE_TAG
    neo_js_vm_super_member_tag,      // NEO_ASM_SUPER_MEMBER_TAG
    neo_js_vm_del_field,             // NEO_ASM_DEL_FIELD
};

neo_js_variable_t neo_js_vm_exec(neo_js_vm_t vm, neo_program_t program) {
  neo_js_scope_t current = neo_js_context_set_scope(vm->ctx, vm->scope);
  neo_allocator_t allocator = neo_js_context_get_allocator(vm->ctx);
  while (true) {
    if (vm->offset == neo_buffer_get_size(program->codes)) {
      if (neo_list_get_size(vm->stack) > 0) {
        neo_js_variable_t goto_interrupt =
            neo_list_node_get(neo_list_get_last(vm->stack));
        if (neo_js_variable_get_type(goto_interrupt)->kind ==
                NEO_JS_TYPE_INTERRUPT &&
            neo_js_variable_to_interrupt(goto_interrupt)->type ==
                NEO_JS_INTERRUPT_GOTO) {
          neo_js_label_frame_t lframe =
              neo_js_context_get_opaque(vm->ctx, goto_interrupt, L"#label");
          if (lframe->try_stack_top != neo_list_get_size(vm->try_stack)) {
            neo_js_try_frame_t frame =
                neo_list_node_get(neo_list_get_last(vm->try_stack));
            goto_interrupt = neo_js_scope_create_variable(
                frame->scope, neo_js_variable_get_chunk(goto_interrupt), NULL);
            neo_js_variable_t result = NULL;
            while (neo_list_get_size(vm->stack) != frame->stack_top) {
              neo_list_pop(vm->stack);
            }
            while (neo_js_context_get_scope(vm->ctx) != frame->scope) {
              neo_js_variable_t res = neo_js_vm_scope_dispose(vm);
              if (neo_js_variable_get_type(res)->kind ==
                  NEO_JS_TYPE_INTERRUPT) {
                neo_list_push(vm->stack, goto_interrupt);
                vm->scope = neo_js_context_set_scope(vm->ctx, current);
                return res;
              } else if (neo_js_variable_get_type(res)->kind ==
                         NEO_JS_TYPE_ERROR) {
                result = neo_js_scope_create_variable(
                    frame->scope, neo_js_variable_get_chunk(res), NULL);
              }
              neo_js_context_pop_scope(vm->ctx);
            }
            if (result) {
              if (frame->onerror) {
                result = neo_js_error_get_error(vm->ctx, result);
                neo_list_push(vm->stack, result);
                vm->offset = frame->onerror;
                frame->onerror = 0;
              } else {
                neo_list_push(vm->stack, result);
                vm->offset = frame->onfinish;
                frame->onfinish = 0;
              }
            } else {
              neo_list_push(vm->stack, goto_interrupt);
              if (frame->onerror) {
                frame->onerror = 0;
              }
              if (frame->onfinish) {
                vm->offset = frame->onfinish;
                frame->onfinish = 0;
              }
            }
            if (!frame->onerror && !frame->onerror &&
                vm->offset == neo_buffer_get_size(program->codes)) {
              neo_list_pop(vm->try_stack);
            }
            continue;
          }
          neo_js_scope_t scope = lframe->scope;
          size_t stack_top = lframe->stack_top;
          vm->offset = lframe->addr;
          neo_js_variable_t result = NULL;
          while (neo_list_get_size(vm->stack) != stack_top) {
            neo_list_pop(vm->stack);
          }
          while (neo_js_context_get_scope(vm->ctx) != scope) {
            neo_js_variable_t res = neo_js_vm_scope_dispose(vm);
            if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_INTERRUPT) {
              neo_list_push(vm->stack, goto_interrupt);
              vm->scope = neo_js_context_set_scope(vm->ctx, current);
              return res;
            } else if (neo_js_variable_get_type(res)->kind ==
                       NEO_JS_TYPE_ERROR) {
              result = neo_js_scope_create_variable(
                  scope, neo_js_variable_get_chunk(res), NULL);
            }
            neo_js_context_pop_scope(vm->ctx);
          }
          if (result) {
            neo_list_push(vm->stack, result);
            vm->offset = neo_buffer_get_size(program->codes);
          }
          continue;
        }
      }
      if (neo_list_get_size(vm->stack) > 0) {
        neo_js_variable_t result =
            neo_list_node_get(neo_list_get_last(vm->stack));
        if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_INTERRUPT) {
          neo_list_pop(vm->stack);
          vm->scope = neo_js_context_set_scope(vm->ctx, current);
          return result;
        }
      }
      if (!neo_list_get_size(vm->try_stack)) {
        break;
      }
      neo_js_try_frame_t frame =
          neo_list_node_get(neo_list_get_last(vm->try_stack));
      while (neo_list_get_size(vm->label_stack) != frame->label_stack_top) {
        neo_js_label_frame_t label =
            neo_list_node_get(neo_list_get_last(vm->label_stack));
        neo_allocator_free(allocator, label);
        neo_list_pop(vm->label_stack);
      }
      neo_js_variable_t result =
          neo_list_node_get(neo_list_get_last(vm->stack));
      result = neo_js_scope_create_variable(
          frame->scope, neo_js_variable_get_chunk(result), NULL);
      while (neo_list_get_size(vm->stack) != frame->stack_top) {
        neo_list_pop(vm->stack);
      }
      while (neo_js_context_get_scope(vm->ctx) != frame->scope) {
        neo_js_variable_t res = neo_js_vm_scope_dispose(vm);
        if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_INTERRUPT) {
          neo_list_push(vm->stack, result);
          vm->scope = neo_js_context_set_scope(vm->ctx, current);
          return res;
        } else if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_ERROR) {
          result = neo_js_scope_create_variable(
              frame->scope, neo_js_variable_get_chunk(res), NULL);
        }
        neo_js_context_pop_scope(vm->ctx);
      }
      neo_list_push(vm->stack, result);
      if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
        if (frame->onerror) {
          result = neo_js_error_get_error(vm->ctx, result);
          neo_list_pop(vm->stack);
          neo_list_push(vm->stack, result);
          vm->offset = frame->onerror;
          frame->onerror = 0;
        } else {
          vm->offset = frame->onfinish;
          frame->onfinish = 0;
        }
      } else {
        if (frame->onerror) {
          frame->onerror = 0;
        }
        if (frame->onfinish) {
          vm->offset = frame->onfinish;
          frame->onfinish = 0;
        }
      }
      if (!frame->onerror && !frame->onerror &&
          vm->offset == neo_buffer_get_size(program->codes)) {
        neo_list_pop(vm->try_stack);
      }
      continue;
    }
    neo_asm_code_t code = neo_js_vm_read_code(vm, program);
    if (cmds[code]) {
      cmds[code](vm, program);
    } else {
      vm->scope = neo_js_context_set_scope(vm->ctx, current);
      return neo_js_context_create_simple_error(vm->ctx, NEO_JS_ERROR_SYNTAX, 0,
                                                L"Unsupport ASM");
    }
  }
  vm->scope = neo_js_context_set_scope(vm->ctx, current);
  neo_js_variable_t result = NULL;
  if (neo_list_get_size(vm->stack)) {
    result = neo_list_node_get(neo_list_get_last(vm->stack));
    result = neo_js_context_create_variable(
        vm->ctx, neo_js_variable_get_chunk(result), NULL);
    neo_list_pop(vm->stack);
  } else {
    result = neo_js_context_create_undefined(vm->ctx);
  }
  current = neo_js_context_set_scope(vm->ctx, vm->scope);
  while (vm->scope != vm->root) {
    neo_js_variable_t res = neo_js_vm_scope_dispose(vm);
    if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_INTERRUPT) {
      result = neo_js_context_create_variable(
          vm->ctx, neo_js_variable_get_chunk(result), NULL);
      neo_list_push(vm->stack, result);
      vm->scope = neo_js_context_set_scope(vm->ctx, current);
      return res;
    } else if (neo_js_variable_get_type(res)->kind == NEO_JS_TYPE_ERROR) {
      result = neo_js_scope_create_variable(
          current, neo_js_variable_get_chunk(res), NULL);
    }
    neo_js_context_pop_scope(vm->ctx);
    vm->scope = neo_js_context_get_scope(vm->ctx);
  }
  for (neo_list_node_t it = neo_list_get_last(vm->active_features);
       it != neo_list_get_head(vm->active_features);
       it = neo_list_node_last(it)) {
    const wchar_t *feature = neo_list_node_get(it);
    neo_js_context_disable(vm->ctx, feature);
  }
  vm->scope = neo_js_context_set_scope(vm->ctx, current);
  return result;
}

neo_list_t neo_js_vm_get_stack(neo_js_vm_t vm) { return vm->stack; }

void neo_js_vm_set_offset(neo_js_vm_t vm, size_t offset) {
  vm->offset = offset;
}
