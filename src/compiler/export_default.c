#include "compiler/export_default.h"

static void neo_ast_export_default_dispose(neo_allocator_t allocator,
                                           neo_ast_export_default_t node) {
  neo_allocator_free(allocator, node->value);
}