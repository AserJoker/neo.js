#include "engine/std/generator.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "engine/basetype/coroutine.h"
#include "engine/basetype/interrupt.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/variable.h"
#include "runtime/vm.h"
neo_js_variable_t neo_js_generator_constructor(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_generator_next(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL, NULL);
  neo_js_variable_t co =
      neo_js_context_get_internal(ctx, self, L"[[coroutine]]");
  neo_js_coroutine_t coroutine = neo_js_variable_to_coroutine(co);
  neo_js_handle_t hcoroutine = neo_js_variable_get_handle(co);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (coroutine->done) {
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"done"),
        neo_js_context_create_boolean(ctx, coroutine->done));
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"value"),
        neo_js_context_create_variable(ctx, coroutine->result, NULL));
  } else {
    neo_js_scope_t current = neo_js_context_set_scope(ctx, coroutine->scope);
    neo_js_variable_t arg = NULL;
    if (argc) {
      arg = neo_js_context_create_variable(
          ctx, neo_js_variable_get_handle(argv[0]), NULL);
    } else {
      arg = neo_js_context_create_undefined(ctx);
    }
    neo_list_t stack = neo_js_vm_get_stack(coroutine->vm);
    neo_list_push(stack, arg);
    neo_js_variable_t value = neo_js_vm_eval(coroutine->vm, coroutine->program);
    coroutine->scope = neo_js_context_set_scope(ctx, current);
    if (neo_js_variable_get_type(value)->kind == NEO_TYPE_INTERRUPT) {
      neo_js_interrupt_t interrupt = neo_js_variable_to_interrupt(value);
      neo_js_vm_set_offset(coroutine->vm, interrupt->offset);
      if (coroutine->result) {
        neo_js_handle_add_parent(
            coroutine->result,
            neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx)));
      }
      neo_js_handle_add_parent(interrupt->result, hcoroutine);
      coroutine->result = interrupt->result;
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"done"),
          neo_js_context_create_boolean(ctx, coroutine->done));
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, coroutine->result, NULL));
    } else if (neo_js_variable_get_type(value)->kind == NEO_TYPE_ERROR) {
      coroutine->done = true;
      if (coroutine->result) {
        neo_js_handle_add_parent(
            coroutine->result,
            neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx)));
      }
      coroutine->result = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(coroutine->result, hcoroutine);
      neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
      result = neo_js_scope_create_variable(
          allocator, current, neo_js_variable_get_handle(value), NULL);
      current = neo_js_context_set_scope(ctx, coroutine->scope);
      while (coroutine->scope != coroutine->root) {
        neo_js_context_pop_scope(ctx);
        coroutine->scope = neo_js_context_get_scope(ctx);
      }
      neo_js_context_set_scope(ctx, current);
    } else {
      coroutine->done = true;
      if (coroutine->result) {
        neo_js_handle_add_parent(
            coroutine->result,
            neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx)));
      }
      coroutine->result = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(coroutine->result, hcoroutine);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"done"),
          neo_js_context_create_boolean(ctx, coroutine->done));
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, coroutine->result, NULL));
      current = neo_js_context_set_scope(ctx, coroutine->scope);
      while (coroutine->scope != coroutine->root) {
        neo_js_context_pop_scope(ctx);
        coroutine->scope = neo_js_context_get_scope(ctx);
      }
      neo_js_context_set_scope(ctx, current);
    }
  }
  return result;
}

neo_js_variable_t neo_js_generator_return(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL, NULL);
  neo_js_variable_t co =
      neo_js_context_get_internal(ctx, self, L"[[coroutine]]");
  neo_js_coroutine_t coroutine = neo_js_variable_to_coroutine(co);
  neo_js_handle_t hcoroutine = neo_js_variable_get_handle(co);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (coroutine->done) {
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"done"),
        neo_js_context_create_boolean(ctx, coroutine->done));
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"value"),
        neo_js_context_create_variable(ctx, coroutine->result, NULL));
  } else {
    neo_js_scope_t current = neo_js_context_set_scope(ctx, coroutine->scope);
    neo_js_variable_t arg = NULL;
    if (argc) {
      arg = neo_js_context_create_variable(
          ctx, neo_js_variable_get_handle(argv[0]), NULL);
    } else {
      arg = neo_js_context_create_undefined(ctx);
    }
    neo_list_t stack = neo_js_vm_get_stack(coroutine->vm);
    neo_js_vm_set_offset(coroutine->vm,
                         neo_buffer_get_size(coroutine->program->codes));
    neo_list_push(stack, arg);
    neo_js_variable_t value = neo_js_vm_eval(coroutine->vm, coroutine->program);
    coroutine->scope = neo_js_context_set_scope(ctx, current);
    if (neo_js_variable_get_type(value)->kind == NEO_TYPE_ERROR) {
      coroutine->done = true;
      if (coroutine->result) {
        neo_js_handle_add_parent(
            coroutine->result,
            neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx)));
      }
      coroutine->result = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(coroutine->result, hcoroutine);
      neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
      result = neo_js_scope_create_variable(
          allocator, current, neo_js_variable_get_handle(value), NULL);
      current = neo_js_context_set_scope(ctx, coroutine->scope);
      while (coroutine->scope != coroutine->root) {
        neo_js_context_pop_scope(ctx);
        coroutine->scope = neo_js_context_get_scope(ctx);
      }
      neo_js_context_set_scope(ctx, current);
    } else {
      coroutine->done = true;
      if (coroutine->result) {
        neo_js_handle_add_parent(
            coroutine->result,
            neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx)));
      }
      coroutine->result = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(coroutine->result, hcoroutine);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"done"),
          neo_js_context_create_boolean(ctx, coroutine->done));
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, coroutine->result, NULL));
      current = neo_js_context_set_scope(ctx, coroutine->scope);
      while (coroutine->scope != coroutine->root) {
        neo_js_context_pop_scope(ctx);
        coroutine->scope = neo_js_context_get_scope(ctx);
      }
      neo_js_context_set_scope(ctx, current);
    }
  }
  return result;
}

neo_js_variable_t neo_js_generator_throw(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL, NULL);
  neo_js_variable_t co =
      neo_js_context_get_internal(ctx, self, L"[[coroutine]]");
  neo_js_coroutine_t coroutine = neo_js_variable_to_coroutine(co);
  neo_js_handle_t hcoroutine = neo_js_variable_get_handle(co);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (coroutine->done) {
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"done"),
        neo_js_context_create_boolean(ctx, coroutine->done));
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"value"),
        neo_js_context_create_variable(ctx, coroutine->result, NULL));
  } else {
    neo_js_scope_t current = neo_js_context_set_scope(ctx, coroutine->scope);
    neo_js_variable_t arg = NULL;
    if (argc) {
      arg = neo_js_context_create_variable(
          ctx, neo_js_variable_get_handle(argv[0]), NULL);
    } else {
      arg = neo_js_context_create_undefined(ctx);
    }
    arg = neo_js_context_create_value_error(ctx, arg);
    neo_list_t stack = neo_js_vm_get_stack(coroutine->vm);
    neo_list_push(stack, arg);
    neo_js_vm_set_offset(coroutine->vm,
                         neo_buffer_get_size(coroutine->program->codes));
    neo_js_variable_t value = neo_js_vm_eval(coroutine->vm, coroutine->program);
    coroutine->scope = neo_js_context_set_scope(ctx, current);
    if (neo_js_variable_get_type(value)->kind == NEO_TYPE_INTERRUPT) {
      neo_js_interrupt_t interrupt = neo_js_variable_to_interrupt(value);
      neo_js_vm_set_offset(coroutine->vm, interrupt->offset);
      if (coroutine->result) {
        neo_js_handle_add_parent(
            coroutine->result,
            neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx)));
      }
      neo_js_handle_add_parent(interrupt->result, hcoroutine);
      coroutine->result = interrupt->result;
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"done"),
          neo_js_context_create_boolean(ctx, coroutine->done));
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, coroutine->result, NULL));
    } else if (neo_js_variable_get_type(value)->kind == NEO_TYPE_ERROR) {
      coroutine->done = true;
      if (coroutine->result) {
        neo_js_handle_add_parent(
            coroutine->result,
            neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx)));
      }
      coroutine->result = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(coroutine->result, hcoroutine);
      neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
      result = neo_js_scope_create_variable(
          allocator, current, neo_js_variable_get_handle(value), NULL);
      current = neo_js_context_set_scope(ctx, coroutine->scope);
      while (coroutine->scope != coroutine->root) {
        neo_js_context_pop_scope(ctx);
        coroutine->scope = neo_js_context_get_scope(ctx);
      }
      neo_js_context_set_scope(ctx, current);
    } else {
      coroutine->done = true;
      if (coroutine->result) {
        neo_js_handle_add_parent(
            coroutine->result,
            neo_js_scope_get_root_handle(neo_js_context_get_scope(ctx)));
      }
      coroutine->result = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(coroutine->result, hcoroutine);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"done"),
          neo_js_context_create_boolean(ctx, coroutine->done));
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, coroutine->result, NULL));
      current = neo_js_context_set_scope(ctx, coroutine->scope);
      while (coroutine->scope != coroutine->root) {
        neo_js_context_pop_scope(ctx);
        coroutine->scope = neo_js_context_get_scope(ctx);
      }
      neo_js_context_set_scope(ctx, current);
    }
  }
  return result;
}