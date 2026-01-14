#include "neo.js/engine/symbol.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/string.h"
#include <stdint.h>
#include <string.h>
static void neo_js_symbol_dispose(neo_allocator_t allocator,
                                  neo_js_symbol_t self) {
  neo_deinit_js_symbol(self, allocator);
}

neo_js_symbol_t neo_create_js_symbol(neo_allocator_t allocator,
                                     const uint16_t *description) {
  neo_js_symbol_t symbol = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_symbol_t), neo_js_symbol_dispose);
  neo_init_js_symbol(symbol, allocator, description);
  return symbol;
}

void neo_init_js_symbol(neo_js_symbol_t self, neo_allocator_t allocator,
                        const uint16_t *description) {
  neo_init_js_value(&self->super, allocator, NEO_JS_TYPE_SYMBOL);
  size_t len = neo_string16_length(description);
  if (description) {
    self->description =
        neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
    memcpy(self->description, description, sizeof(uint16_t) * (len + 1));
  } else {
    self->description = NULL;
  }
}

void neo_deinit_js_symbol(neo_js_symbol_t self, neo_allocator_t allocator) {
  neo_allocator_free(allocator, self->description);
  neo_deinit_js_value(&self->super, allocator);
}
neo_js_value_t neo_js_symbol_to_value(neo_js_symbol_t self) {
  return &self->super;
}