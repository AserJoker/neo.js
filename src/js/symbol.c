#include "js/symbol.h"
#include "core/allocator.h"
#include "js/context.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>

static const wchar_t *neo_js_symbol_typeof(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  return L"symbol";
}

static neo_js_variable_t neo_js_symbol_to_string(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  return neo_js_context_create_error(
      ctx, L"TypeError", L"Cannot convert a Symbol value to a string");
}

static neo_js_variable_t neo_js_symbol_to_boolean(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  return neo_js_context_create_boolean(ctx, true);
}

static neo_js_variable_t neo_js_symbol_to_number(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  return neo_js_context_create_error(
      ctx, L"TypeError", L"Cannot convert a Symbol value to a number");
}

neo_js_type_t neo_get_js_symbol_type() {
  static struct _neo_js_type_t type = {
      neo_js_symbol_typeof,
      neo_js_symbol_to_string,
      neo_js_symbol_to_boolean,
      neo_js_symbol_to_number,
  };
  return &type;
}

static void neo_js_symbol_dispose(neo_allocator_t allocator,
                                  neo_js_symbol_t self) {
  neo_allocator_free(allocator, self->description);
}

neo_js_symbol_t neo_create_js_symbol(neo_allocator_t allocator,
                                     const wchar_t *description) {
  if (!description) {
    description = L"";
  }
  size_t len = wcslen(description);
  neo_js_symbol_t symbol = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_symbol_t), neo_js_symbol_dispose);
  symbol->value.type = neo_get_js_symbol_type();
  symbol->value.ref = 0;
  symbol->description =
      neo_allocator_alloc(allocator, (len + 1) * sizeof(wchar_t), NULL);
  wcscpy(symbol->description, description);
  symbol->description[len] = 0;
  return symbol;
}
neo_js_symbol_t neo_js_value_to_symbol(neo_js_value_t value) {
  if (value->type == neo_get_js_symbol_type()) {
    return (neo_js_symbol_t)value;
  }
  return NULL;
}