
#include "neojs/runtime/iterator.h"
#include "neojs/engine/context.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
NEO_JS_CFUNCTION(neo_js_iterator_constructor) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t message = neo_js_context_create_string(
      ctx, u"Abstract class Iterator not directly constructable");
  neo_js_variable_t error =
      neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
  return neo_js_context_create_exception(ctx, error);
}
NEO_JS_CFUNCTION(neo_js_iterator_iterator) { return self; }
void neo_initialize_js_iterator(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->iterator_class = neo_js_context_create_cfunction(
      ctx, neo_js_iterator_constructor, "Iterator");
  constant->iterator_prototype = neo_js_variable_get_field(
      constant->iterator_class, ctx, constant->key_prototype);
  neo_js_variable_t iterator = neo_js_context_create_cfunction(
      ctx, neo_js_iterator_iterator, "[Symbol.iterator]");
  neo_js_variable_set_field(constant->iterator_prototype, ctx,
                            constant->symbol_iterator, iterator);
}