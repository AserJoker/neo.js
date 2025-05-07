#ifndef _H_NOIX_COMPILER_PARSER_
#define _H_NOIX_COMPILER_PARSER_
#include "compiler/node.h"
#include "core/allocator.h"
#ifdef __cplusplus
extern "C" {
#endif

noix_ast_node_t noix_parse_code(noix_allocator_t allocator, const char *file,
                                const char *source);

#ifdef __cplusplus
}
#endif
#endif