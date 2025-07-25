#ifndef _H_NEO_COMPILER_DECLARATION_IMPORT_
#define _H_NEO_COMPILER_DECLARATION_IMPORT_
#include "compiler/ast/node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_declaration_import_t {
  struct _neo_ast_node_t node;
  neo_list_t specifiers;
  neo_ast_node_t source;
  neo_list_t attributes;
} *neo_ast_declaration_import_t;

neo_ast_node_t neo_ast_read_declaration_import(neo_allocator_t allocator,
                                               const wchar_t *file,
                                               neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif