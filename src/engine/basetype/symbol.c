#include "engine/basetype/symbol.h"
#include "core/allocator.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>

static const wchar_t *neo_engine_symbol_typeof(neo_engine_context_t ctx,
                                               neo_engine_variable_t variable) {
  return L"symbol";
}

static neo_engine_variable_t
neo_engine_symbol_to_string(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  return neo_engine_context_create_error(
      ctx, L"TypeError", L"Cannot convert a Symbol value to a string");
}

static neo_engine_variable_t
neo_engine_symbol_to_boolean(neo_engine_context_t ctx,
                             neo_engine_variable_t self) {
  return neo_engine_context_create_boolean(ctx, true);
}

static neo_engine_variable_t
neo_engine_symbol_to_number(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  return neo_engine_context_create_error(
      ctx, L"TypeError", L"Cannot convert a Symbol value to a number");
}

static neo_engine_variable_t
neo_engine_symbol_to_primitive(neo_engine_context_t ctx,
                               neo_engine_variable_t self) {
  return self;
}

static neo_engine_variable_t
neo_engine_symbol_to_object(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  return neo_engine_context_construct(
      ctx, neo_engine_context_get_symbol_constructor(ctx), 1, &self);
}

static neo_engine_variable_t
neo_engine_symbol_get_field(neo_engine_context_t ctx,
                            neo_engine_variable_t self,
                            neo_engine_variable_t field) {
  return neo_engine_context_get_field(
      ctx, neo_engine_symbol_to_object(ctx, self), field);
}

static neo_engine_variable_t neo_engine_symbol_set_field(
    neo_engine_context_t ctx, neo_engine_variable_t self,
    neo_engine_variable_t field, neo_engine_variable_t value) {
  return neo_engine_context_set_field(
      ctx, neo_engine_symbol_to_object(ctx, self), field, value);
}

static neo_engine_variable_t
neo_engine_symbol_del_field(neo_engine_context_t ctx,
                            neo_engine_variable_t self,
                            neo_engine_variable_t field) {
  return neo_engine_context_del_field(
      ctx, neo_engine_symbol_to_object(ctx, self), field);
}

static bool neo_engine_symbol_is_equal(neo_engine_context_t ctx,
                                       neo_engine_variable_t self,
                                       neo_engine_variable_t another) {
  return neo_engine_variable_get_value(self) ==
         neo_engine_variable_get_value(another);
}

static void neo_engine_symbol_copy(neo_engine_context_t ctx,
                                   neo_engine_variable_t self,
                                   neo_engine_variable_t another) {
  neo_allocator_t allocaotr =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  neo_engine_handle_set_value(allocaotr,
                              neo_engine_variable_get_handle(another),
                              neo_engine_variable_get_value(self));
}

neo_engine_type_t neo_get_js_symbol_type() {
  static struct _neo_engine_type_t type = {
      NEO_TYPE_SYMBOL,
      neo_engine_symbol_typeof,
      neo_engine_symbol_to_string,
      neo_engine_symbol_to_boolean,
      neo_engine_symbol_to_number,
      neo_engine_symbol_to_primitive,
      neo_engine_symbol_to_object,
      neo_engine_symbol_get_field,
      neo_engine_symbol_set_field,
      neo_engine_symbol_del_field,
      neo_engine_symbol_is_equal,
      neo_engine_symbol_copy,
  };
  return &type;
}

static void neo_engine_symbol_dispose(neo_allocator_t allocator,
                                      neo_engine_symbol_t self) {
  neo_allocator_free(allocator, self->description);
}

neo_engine_symbol_t neo_create_js_symbol(neo_allocator_t allocator,
                                         const wchar_t *description) {
  if (!description) {
    description = L"";
  }
  size_t len = wcslen(description);
  neo_engine_symbol_t symbol =
      neo_allocator_alloc(allocator, sizeof(struct _neo_engine_symbol_t),
                          neo_engine_symbol_dispose);
  symbol->value.type = neo_get_js_symbol_type();
  symbol->value.ref = 0;
  symbol->description =
      neo_allocator_alloc(allocator, (len + 1) * sizeof(wchar_t), NULL);
  wcscpy(symbol->description, description);
  symbol->description[len] = 0;
  return symbol;
}
neo_engine_symbol_t neo_engine_value_to_symbol(neo_engine_value_t value) {
  if (value->type->kind == NEO_TYPE_SYMBOL) {
    return (neo_engine_symbol_t)value;
  }
  return NULL;
}