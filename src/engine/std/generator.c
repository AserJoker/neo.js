#include "engine/std/generator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "engine/basetype/coroutine.h"
#include "engine/basetype/interrupt.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/variable.h"
#include "runtime/vm.h"

neo_js_variable_t neo_js_generator_iterator(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  return self;
}

neo_js_variable_t neo_js_generator_constructor(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_generator_next(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_js_variable_t coroutine =
      neo_js_context_get_internal(ctx, self, L"[[coroutine]]");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  if (co_ctx->running) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"Generator is running");
  }
  if (co_ctx->result) {
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"done"),
                             neo_js_context_create_boolean(ctx, true), NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"value"),
        neo_js_context_create_variable(ctx, co_ctx->result, NULL), NULL);
  } else {
    neo_js_scope_t current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
    neo_js_variable_t arg = NULL;
    if (argc) {
      arg = neo_js_context_create_variable(
          ctx, neo_js_variable_getneo_create_js_chunk(argv[0]), NULL);
    } else {
      arg = neo_js_context_create_undefined(ctx);
    }
    neo_js_context_set_scope(ctx, current);
    neo_list_push(co_ctx->vm->stack, arg);
    co_ctx->running = true;
    neo_js_variable_t value = neo_js_vm_exec(co_ctx->vm, co_ctx->program);
    co_ctx->running = false;
    if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_INTERRUPT) {
      neo_js_interrupt_t interrupt = neo_js_variable_to_interrupt(value);
      co_ctx->vm->offset = interrupt->offset;
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, L"done"),
                               neo_js_context_create_boolean(ctx, false), NULL);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, interrupt->result, NULL), NULL);
    } else if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
      co_ctx->result = neo_js_variable_getneo_create_js_chunk(value);
      neo_js_chunk_add_parent(co_ctx->result,
                              neo_js_variable_getneo_create_js_chunk(self));
      neo_js_context_recycle_coroutine(ctx, coroutine);
    } else {
      co_ctx->result = neo_js_variable_getneo_create_js_chunk(value);
      neo_js_chunk_add_parent(co_ctx->result,
                              neo_js_variable_getneo_create_js_chunk(self));
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, L"done"),
                               neo_js_context_create_boolean(ctx, true), NULL);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, co_ctx->result, NULL), NULL);
      neo_js_context_recycle_coroutine(ctx, coroutine);
    }
  }
  return result;
}

neo_js_variable_t neo_js_generator_return(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_js_variable_t coroutine =
      neo_js_context_get_internal(ctx, self, L"[[coroutine]]");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  if (co_ctx->running) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"Generator is running");
  }
  if (co_ctx->result) {
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"done"),
                             neo_js_context_create_boolean(ctx, true), NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"value"),
        neo_js_context_create_variable(ctx, co_ctx->result, NULL), NULL);
  } else {
    neo_js_scope_t current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
    neo_js_variable_t arg = NULL;
    if (argc) {
      arg = neo_js_context_create_variable(
          ctx, neo_js_variable_getneo_create_js_chunk(argv[0]), NULL);
    } else {
      arg = neo_js_context_create_undefined(ctx);
    }
    neo_js_context_set_scope(ctx, current);
    neo_list_push(co_ctx->vm->stack, arg);
    co_ctx->vm->offset = neo_buffer_get_size(co_ctx->program->codes);
    co_ctx->running = true;
    neo_js_variable_t value = neo_js_vm_exec(co_ctx->vm, co_ctx->program);
    co_ctx->running = false;
    if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_INTERRUPT) {
      neo_js_interrupt_t interrupt = neo_js_variable_to_interrupt(value);
      co_ctx->vm->offset = interrupt->offset;
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, L"done"),
                               neo_js_context_create_boolean(ctx, false), NULL);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, interrupt->result, NULL), NULL);
    } else if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
      co_ctx->result = neo_js_variable_getneo_create_js_chunk(value);
      neo_js_chunk_add_parent(co_ctx->result,
                              neo_js_variable_getneo_create_js_chunk(self));
      neo_js_context_recycle_coroutine(ctx, coroutine);
    } else {
      co_ctx->result = neo_js_variable_getneo_create_js_chunk(value);
      neo_js_chunk_add_parent(co_ctx->result,
                              neo_js_variable_getneo_create_js_chunk(self));
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, L"done"),
                               neo_js_context_create_boolean(ctx, true), NULL);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, co_ctx->result, NULL), NULL);
      neo_js_context_recycle_coroutine(ctx, coroutine);
    }
  }
  return result;
}

neo_js_variable_t neo_js_generator_throw(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_js_variable_t coroutine =
      neo_js_context_get_internal(ctx, self, L"[[coroutine]]");
  neo_js_co_context_t co_ctx = neo_js_coroutine_get_context(coroutine);
  if (co_ctx->running) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE,
                                              L"Generator is running");
  }
  if (co_ctx->result) {
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, L"done"),
                             neo_js_context_create_boolean(ctx, true), NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, L"value"),
        neo_js_context_create_variable(ctx, co_ctx->result, NULL), NULL);
  } else {
    neo_js_scope_t current = neo_js_context_set_scope(ctx, co_ctx->vm->scope);
    neo_js_variable_t arg = NULL;
    if (argc) {
      arg = neo_js_context_create_error(ctx, argv[0]);
    } else {
      arg = neo_js_context_create_error(
          ctx,
          neo_js_context_construct(
              ctx, neo_js_context_get_std(ctx).error_constructor, 0, NULL));
    }
    neo_js_context_set_scope(ctx, current);
    neo_list_push(co_ctx->vm->stack, arg);
    co_ctx->vm->offset = neo_buffer_get_size(co_ctx->program->codes);
    co_ctx->running = true;
    neo_js_variable_t value = neo_js_vm_exec(co_ctx->vm, co_ctx->program);
    co_ctx->running = false;
    if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_INTERRUPT) {
      neo_js_interrupt_t interrupt = neo_js_variable_to_interrupt(value);
      co_ctx->vm->offset = interrupt->offset;
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, L"done"),
                               neo_js_context_create_boolean(ctx, false), NULL);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, interrupt->result, NULL), NULL);
    } else if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
      co_ctx->result = neo_js_variable_getneo_create_js_chunk(value);
      neo_js_chunk_add_parent(co_ctx->result,
                              neo_js_variable_getneo_create_js_chunk(self));
      neo_js_context_recycle_coroutine(ctx, coroutine);
    } else {
      co_ctx->result = neo_js_variable_getneo_create_js_chunk(value);
      neo_js_chunk_add_parent(co_ctx->result,
                              neo_js_variable_getneo_create_js_chunk(self));
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, L"done"),
                               neo_js_context_create_boolean(ctx, true), NULL);
      neo_js_context_set_field(
          ctx, result, neo_js_context_create_string(ctx, L"value"),
          neo_js_context_create_variable(ctx, co_ctx->result, NULL), NULL);
      neo_js_context_recycle_coroutine(ctx, coroutine);
    }
  }
  return result;
}