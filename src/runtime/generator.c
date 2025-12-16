#include "runtime/generator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "engine/context.h"
#include "engine/interrupt.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/vm.h"

NEO_JS_CFUNCTION(neo_js_generator_next) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_context_type_t origin_ctx_type =
      neo_js_context_set_type(ctx, NEO_JS_CONTEXT_GENERATOR_FUNCTION);
  if (value->value->type == NEO_JS_TYPE_INTERRUPT) {
    neo_js_interrupt_t interrupt = (neo_js_interrupt_t)value->value;
    neo_js_scope_t current = neo_js_context_set_scope(ctx, interrupt->scope);
    neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
    neo_list_push(interrupt->vm->stack, arg);
    value = neo_js_vm_run(interrupt->vm, ctx, interrupt->program,
                          interrupt->address);
    neo_js_variable_t done = neo_js_context_get_false(ctx);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      result = value;
      neo_js_scope_set_variable(current, result, NULL);
      neo_js_context_pop_scope(ctx);
      value = neo_js_context_get_undefined(ctx);
    } else if (value->value->type == NEO_JS_TYPE_INTERRUPT) {
      interrupt->vm = NULL;
      interrupt = (neo_js_interrupt_t)value->value;
      neo_js_variable_t value =
          neo_js_context_create_variable(ctx, interrupt->value);
      neo_js_variable_set_field(
          result, ctx, neo_js_context_create_cstring(ctx, "value"), value);
      neo_js_variable_set_field(result, ctx,
                                neo_js_context_create_cstring(ctx, "done"),
                                neo_js_context_get_false(ctx));
    } else {
      neo_js_variable_set_field(
          result, ctx, neo_js_context_create_cstring(ctx, "value"), value);
      neo_js_variable_set_field(result, ctx,
                                neo_js_context_create_cstring(ctx, "done"),
                                neo_js_context_get_true(ctx));
      neo_js_context_pop_scope(ctx);
    }
    neo_js_variable_set_internal(self, ctx, "value", value);
    neo_js_context_set_scope(ctx, current);
  } else {
    neo_js_variable_set_field(
        result, ctx, neo_js_context_create_cstring(ctx, "value"), value);
    neo_js_variable_set_field(result, ctx,
                              neo_js_context_create_cstring(ctx, "done"),
                              neo_js_context_get_true(ctx));
  }
  neo_js_context_set_type(ctx, origin_ctx_type);
  return result;
}
NEO_JS_CFUNCTION(neo_js_generator_return) {
  neo_js_variable_t value = neo_js_variable_get_internel(self, ctx, "value");
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_context_type_t origin_ctx_type =
      neo_js_context_set_type(ctx, NEO_JS_CONTEXT_GENERATOR_FUNCTION);
  if (value->value->type == NEO_JS_TYPE_INTERRUPT) {
    neo_js_interrupt_t interrupt = (neo_js_interrupt_t)value->value;
    neo_js_scope_t current = neo_js_context_set_scope(ctx, interrupt->scope);
    neo_js_variable_t arg = neo_js_context_get_argument(ctx, argc, argv, 0);
    interrupt->vm->result = arg;
    value = neo_js_vm_run(interrupt->vm, ctx, interrupt->program,
                          neo_buffer_get_size(interrupt->program->codes));
    neo_js_variable_t done = neo_js_context_get_false(ctx);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      result = value;
      neo_js_scope_set_variable(current, result, NULL);
      neo_js_context_pop_scope(ctx);
      value = neo_js_context_get_undefined(ctx);
    } else if (value->value->type == NEO_JS_TYPE_INTERRUPT) {
      interrupt->vm = NULL;
      interrupt = (neo_js_interrupt_t)value->value;
      neo_js_variable_t value =
          neo_js_context_create_variable(ctx, interrupt->value);
      neo_js_variable_set_field(
          result, ctx, neo_js_context_create_cstring(ctx, "value"), value);
      neo_js_variable_set_field(result, ctx,
                                neo_js_context_create_cstring(ctx, "done"),
                                neo_js_context_get_false(ctx));
    } else {
      neo_js_variable_set_field(
          result, ctx, neo_js_context_create_cstring(ctx, "value"), value);
      neo_js_variable_set_field(result, ctx,
                                neo_js_context_create_cstring(ctx, "done"),
                                neo_js_context_get_true(ctx));
      neo_js_context_pop_scope(ctx);
    }
    neo_js_variable_set_internal(self, ctx, "value", value);
    neo_js_context_set_scope(ctx, current);
  } else {
    neo_js_variable_set_field(
        result, ctx, neo_js_context_create_cstring(ctx, "value"), value);
    neo_js_variable_set_field(result, ctx,
                              neo_js_context_create_cstring(ctx, "done"),
                              neo_js_context_get_true(ctx));
  }
  neo_js_context_set_type(ctx, origin_ctx_type);
  return result;
}
NEO_JS_CFUNCTION(neo_js_generator_throw) {
  neo_js_variable_t args[1];
  args[0] = neo_js_context_get_argument(ctx, argc, argv, 0);
  args[0] = neo_js_context_create_exception(ctx, args[0]);
  return neo_js_generator_return(ctx, self, 1, args);
}
void neo_initialize_js_generator(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->generator_prototype =
      neo_js_context_create_object(ctx, constant->iterator_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->generator_prototype, "next",
                    neo_js_generator_next);
  NEO_JS_DEF_METHOD(ctx, constant->generator_prototype, "return",
                    neo_js_generator_return);
  NEO_JS_DEF_METHOD(ctx, constant->generator_prototype, "throw",
                    neo_js_generator_throw);
  neo_js_variable_t string_tag =
      neo_js_context_create_cstring(ctx, "Generator");
  neo_js_variable_set_field(constant->generator_prototype, ctx,
                            constant->symbol_to_string_tag, string_tag);
}