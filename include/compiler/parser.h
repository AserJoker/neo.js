#ifndef _H_NEO_COMPILER_PARSER_
#define _H_NEO_COMPILER_PARSER_
#include "compiler/ast_node.h"
#include "compiler/program.h"
#include "core/allocator.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_ast_node_t neo_ast_parse_code(neo_allocator_t allocator, const char *file,
                                  const char *source);

neo_js_program_t neo_ast_write_node(neo_allocator_t allocator, const char *file,
                                    neo_ast_node_t node);

#ifdef __cplusplus
}
#endif
#endif