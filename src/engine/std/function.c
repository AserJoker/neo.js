#include "engine/std/function.h"
#include "core/allocator.h"
#include "engine/basetype/callable.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"
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
  if (type->kind == NEO_TYPE_OBJECT) {
    self = neo_js_context_get_internal(ctx, self, L"[[primitive]]");
  }
  type = neo_js_variable_get_type(self);
  if (neo_js_variable_get_type(self)->kind < NEO_TYPE_CALLABLE) {
    return neo_js_context_create_error(
        ctx, L"TypeError",
        L" Function.prototype.toString requires that 'this' be a Function");
  }
  neo_js_callable_t callable = neo_js_variable_to_callable(self);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_string_t string =
      neo_js_value_to_string(neo_js_handle_get_value(callable->name));
  if (type->kind == NEO_TYPE_CFUNCTION) {
    wchar_t *str = neo_allocator_alloc(
        allocator, sizeof(wchar_t) * (wcslen(string->string) + 32), NULL);
    swprintf(str, 32 + wcslen(string->string),
             L"cfunction %ls(){ [native code] }", string->string);
    neo_js_variable_t result = neo_js_context_create_string(ctx, str);
    neo_allocator_free(allocator, str);
    return result;
  } else if (type->kind == NEO_TYPE_FUNCTION) {
    neo_js_function_t func = neo_js_variable_to_function(self);
    return neo_js_context_create_string(ctx, func->source);
  }
  return neo_js_context_create_string(ctx, L"[cfunction Function]");
}