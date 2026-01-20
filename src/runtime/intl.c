#include "neojs/runtime/intl.h"
#include "neojs/engine/context.h"
#include "neojs/runtime/constant.h"

void neo_initialize_js_intl(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->intl = neo_js_context_create_object(ctx, NULL);
}