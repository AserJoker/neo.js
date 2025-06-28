#include "engine/std/array.h"
#include "core/allocator.h"
#include "core/list.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/object.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <wchar.h>

neo_js_variable_t neo_js_array_constructor(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_array_to_string(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_variable_t length = neo_js_context_get_field(
      ctx, self, neo_js_context_create_string(ctx, L"length"));
  int64_t len = neo_js_variable_to_number(length)->number;
  size_t strlen = 0;
  neo_list_t list = neo_create_list(allocator, NULL);
  for (int64_t idx = 0; idx < len; idx++) {
    neo_js_variable_t field = neo_js_context_create_number(ctx, idx);
    neo_js_object_property_t prop =
        neo_js_object_get_property(ctx, self, field);
    if (prop) {
      neo_js_variable_t item = neo_js_context_get_field(ctx, self, field);
      if (neo_js_variable_get_type(item) != neo_js_variable_get_type(self)) {
        neo_js_variable_t result = neo_js_context_is_equal(ctx, item, self);
        neo_js_boolean_t boolean = neo_js_variable_to_boolean(result);
        if (!boolean->boolean) {
          item = neo_js_context_to_string(ctx, item);
          neo_js_string_t string = neo_js_variable_to_string(item);
          neo_list_push(list, string->string);
          strlen += wcslen(string->string);
        }
      }
    } else {
      neo_list_push(list, "");
    }
    if (idx != len - 1) {
      strlen += 1;
    }
  }
  wchar_t *str =
      neo_allocator_alloc(allocator, (strlen + 1) * sizeof(wchar_t), NULL);
  wchar_t *pstr = str;
  str[strlen] = 0;
  for (neo_list_node_t it = neo_list_get_first(list);
       it != neo_list_get_tail(list); it = neo_list_node_next(it)) {
    if (it != neo_list_get_first(list)) {
      *pstr = L',';
      pstr++;
    }
    wchar_t *s = neo_list_node_get(it);
    if (s) {
      while (*s != 0) {
        *pstr = *s;
        pstr++;
        s++;
      }
    }
  }
  neo_allocator_free(allocator, list);
  neo_js_variable_t result = neo_js_context_create_string(ctx, str);
  neo_allocator_free(allocator, str);
  return result;
}

neo_js_variable_t neo_js_array_values(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  neo_js_variable_t iterator = neo_js_context_construct(
      ctx, neo_js_context_get_array_iterator_constructor(ctx), 0, NULL);
  neo_js_variable_t index = neo_js_context_create_number(ctx, 0);
  neo_js_context_set_internal(ctx, iterator, L"[[array]]", self);
  neo_js_context_set_internal(ctx, iterator, L"[[index]]", index);
  return iterator;
}