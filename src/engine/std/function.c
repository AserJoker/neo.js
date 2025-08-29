#include "engine/std/function.h"
#include "core/allocator.h"
#include "engine/basetype/callable.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <stdint.h>
#include <wchar.h>


neo_js_variable_t neo_js_function_constructor(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              uint32_t argc,
                                              neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_function_to_string(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  neo_js_type_t type = neo_js_variable_get_type(self);
  if (neo_js_variable_get_type(self)->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L" Function.prototype.toString requires that 'this' be a Function");
  }
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  if (type->kind == NEO_JS_TYPE_CFUNCTION) {
    const wchar_t *name = callable->name;
    if (!name) {
      name = L"(anonymouse)";
    }
    wchar_t *str = neo_allocator_alloc(
        allocator, sizeof(wchar_t) * (wcslen(name) + 32), NULL);
    swprintf(str, 32 + wcslen(name), L"function %ls(){ [native code] }", name);
    neo_js_variable_t result = neo_js_context_create_string(ctx, str);
    neo_allocator_free(allocator, str);
    return result;
  } else if (type->kind == NEO_JS_TYPE_ASYNC_CFUNCTION) {
    const wchar_t *name = callable->name;
    if (!name) {
      name = L"(anonymouse)";
    }
    wchar_t *str = neo_allocator_alloc(
        allocator, sizeof(wchar_t) * (wcslen(name) + 32), NULL);
    swprintf(str, 32 + wcslen(name), L"async function %ls(){ [native code] }",
             name);
    neo_js_variable_t result = neo_js_context_create_string(ctx, str);
    neo_allocator_free(allocator, str);
    return result;
  } else {
    neo_js_function_t func = neo_js_variable_to_function(self);
    return neo_js_context_create_string(ctx, func->source);
  }
}

neo_js_variable_t neo_js_function_call(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  if (argc > 0) {
    return neo_js_context_call(ctx, self, argv[0], argc - 1, &argv[1]);
  } else {
    return neo_js_context_call(ctx, self, neo_js_context_create_undefined(ctx),
                               0, NULL);
  }
}

NEO_JS_CFUNCTION(neo_js_function_apply) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t self_object = NULL;
  neo_js_variable_t arguments = NULL;
  if (argc) {
    self_object = argv[0];
  } else {
    self_object = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t *args = NULL;
  uint32_t argc2 = 0;
  if (arguments) {
    neo_js_variable_t vlength = neo_js_context_get_field(
        ctx, arguments, neo_js_context_create_string(ctx, L"length"), NULL);
    NEO_JS_TRY_AND_THROW(vlength);
    vlength = neo_js_context_to_integer(ctx, vlength);
    argc2 = neo_js_variable_to_number(vlength)->number;
    args = neo_allocator_alloc(allocator, argc2, NULL);
    for (uint32_t idx = 0; idx < argc2; idx++) {
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t field =
          neo_js_context_get_field(ctx, arguments, key, NULL);
      NEO_JS_TRY_AND_THROW(field);
      args[idx] = field;
    }
  }
  return neo_js_context_call(ctx, self, self_object, argc2, args);
}

neo_js_variable_t neo_js_function_bind(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  neo_js_variable_t result = neo_js_context_create_undefined(ctx);
  NEO_JS_TRY_AND_THROW(neo_js_context_copy(ctx, self, result));
  neo_js_variable_t bind = NULL;
  if (argc > 0) {
    bind = argv[0];
  } else {
    bind = neo_js_context_create_undefined(ctx);
  }
  neo_js_callable_t callable = neo_js_variable_to_callable(result);
  if (!callable->bind) {
    callable->bind = neo_js_variable_get_handle(bind);
    neo_js_chunk_add_parent(callable->bind, neo_js_variable_get_handle(self));
  }
  return result;
}