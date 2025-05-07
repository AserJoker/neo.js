#ifndef _H_NOIX_COMPILER_NODE_
#define _H_NOIX_COMPILER_NODE_

#include "core/location.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum _noix_ast_node_type_t {
  NOIX_NODE_TYPE_IDENTIFIER,
  NOIX_NODE_TYPE_PRIVATE_NAME,
  NOIX_NODE_TYPE_LITERAL_REGEXP,
  NOIX_NODE_TYPE_LITERAL_NULL,
  NOIX_NODE_TYPE_LITERAL_STRING,
  NOIX_NODE_TYPE_LITERAL_BOOLEAN,
  NOIX_NODE_TYPE_LITERAL_NUMERIC,
  NOIX_NODE_TYPE_LITERAL_BIGINT,
  NOIX_NODE_TYPE_LITERAL_DECIMAL,
  NOIX_NODE_TYPE_LITERAL_TEMPLATE,
  NOIX_NODE_TYPE_PROGRAM,
  NOIX_NODE_TYPE_STATEMENT_EXPRESSION,
  NOIX_NODE_TYPE_STATEMENT_BLOCK,
  NOIX_NODE_TYPE_STATEMENT_EMPTY,
  NOIX_NODE_TYPE_STATEMENT_DEBUGGER,
  NOIX_NODE_TYPE_STATEMENT_WITH,
  NOIX_NODE_TYPE_STATEMENT_RETURN,
  NOIX_NODE_TYPE_STATEMENT_LABELED,
  NOIX_NODE_TYPE_STATEMENT_BREAK,
  NOIX_NODE_TYPE_STATEMENT_CONTINUE,
  NOIX_NODE_TYPE_STATEMENT_IF,
  NOIX_NODE_TYPE_STATEMENT_SWITCH,
  NOIX_NODE_TYPE_STATEMENT_SWITCH_CASE,
  NOIX_NODE_TYPE_STATEMENT_THROW,
  NOIX_NODE_TYPE_STATEMENT_TRY,
  NOIX_NODE_TYPE_STATEMENT_TRY_CATCH,
  NOIX_NODE_TYPE_STATEMENT_WHILE,
  NOIX_NODE_TYPE_STATEMENT_DO_WHILE,
  NOIX_NODE_TYPE_STATEMENT_FOR,
  NOIX_NODE_TYPE_STATEMENT_FOR_IN,
  NOIX_NODE_TYPE_STATEMENT_FOR_OF,
  NOIX_NODE_TYPE_STATEMENT_FOR_AWAIT_OF,
  NOIX_NODE_TYPE_DECLARATION_FUNCTION,
  NOIX_NODE_TYPE_DECLARATION_VARIABLE,
  NOIX_NODE_TYPE_DECLARATION_VARIABLE_DECLARATOR,
  NOIX_NODE_TYPE_DECLARATION_IMPORT,
  NOIX_NODE_TYPE_DECLARATION_EXPORT,
  NOIX_NODE_TYPE_DECLARATION_EXPORT_DEFAULT,
  NOIX_NODE_TYPE_DECLARATION_EXPORT_ALL,
  NOIX_NODE_TYPE_DECORATOR,
  NOIX_NODE_TYPE_DIRECTIVE,
  NOIX_NODE_TYPE_INTERPRETER_DIRECTIVE,
  NOIX_NODE_TYPE_EXPRESSION_SUPER,
  NOIX_NODE_TYPE_EXPRESSION_THIS,
  NOIX_NODE_TYPE_EXPRESSION_ARROW_FUNCTION,
  NOIX_NODE_TYPE_EXPRESSION_YIELD,
  NOIX_NODE_TYPE_EXPRESSION_AWAIT,
  NOIX_NODE_TYPE_EXPRESSION_ARRAY,
  NOIX_NODE_TYPE_EXPRESSION_OBJECT,
  NOIX_NODE_TYPE_EXPRESSION_RECORD,
  NOIX_NODE_TYPE_EXPRESSION_TUPLE,
  NOIX_NODE_TYPE_EXPRESSION_FUNCTION,
  NOIX_NODE_TYPE_EXPRESSION_UNARY,
  NOIX_NODE_TYPE_EXPRESSION_UPDATE,
  NOIX_NODE_TYPE_EXPRESSION_BINARY,
  NOIX_NODE_TYPE_EXPRESSION_ASSIGMENT,
  NOIX_NODE_TYPE_EXPRESSION_LOGICAL,
  NOIX_NODE_TYPE_EXPRESSION_SPREAD,
  NOIX_NODE_TYPE_EXPRESSION_MEMBER,
  NOIX_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER,
  NOIX_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER,
  NOIX_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER,
  NOIX_NODE_TYPE_EXPRESSION_CONDITION,
  NOIX_NODE_TYPE_EXPRESSION_CALL,
  NOIX_NODE_TYPE_EXPRESSION_OPTIONAL_CALL,
  NOIX_NODE_TYPE_EXPRESSION_NEW,
  NOIX_NODE_TYPE_EXPRESSION_SEQUENCE,
  NOIX_NODE_TYPE_EXPRESSION_GROUP,
  NOIX_NODE_TYPE_PATTERN_OBJECT,
  NOIX_NODE_TYPE_PATTERN_ARRAY,
  NOIX_NODE_TYPE_PATTERN_REST,
  NOIX_NODE_TYPE_PATTERN_ASSIGMENT,
  NOIX_NODE_TYPE_PATTERN_CLASS,
  NOIX_NODE_TYPE_CLASS_METHOD,
  NOIX_NODE_TYPE_CLASS_PROPERTY,
  NOIX_NODE_TYPE_CLASS_STATIC_BLOCK,
  NOIX_NODE_TYPE_IMPORT_SPECIFIER,
  NOIX_NODE_TYPE_IMPORT_DEFAULT_SPECIFIER,
  NOIX_NODE_TYPE_IMPORT_NAMESPACE_SPECIFIER,
  NOIX_NODE_TYPE_IMPORT_ATTRIBUTE,
  NOIX_NODE_TYPE_EXPORT_SPECIFIER,
  NOIX_NODE_TYPE_EXPORT_NAMESPACE_SPECIFIER,
} noix_ast_node_type_t;

typedef struct _noix_ast_node_t {
  noix_ast_node_type_t type;
  noix_location_t location;
} *noix_ast_node_t;

bool noix_skip_white_space(noix_allocator_t allocator, const char *file,
                           noix_position_t *position);

bool noix_skip_line_terminator(noix_allocator_t allocator, const char *file,
                               noix_position_t *position);

bool noix_skip_comment(noix_allocator_t allocator, const char *file,
                       noix_position_t *position);
#define SKIP_ALL()                                                             \
  for (;;) {                                                                   \
    if (noix_skip_white_space(allocator, file, &current)) {                    \
      continue;                                                                \
    }                                                                          \
    if (noix_skip_line_terminator(allocator, file, &current)) {                \
      continue;                                                                \
    }                                                                          \
    TRY(if (noix_skip_comment(allocator, file, &current)) { continue; }) {     \
      goto onerror;                                                            \
    }                                                                          \
    break;                                                                     \
  }

#ifdef __cplusplus
}
#endif
#endif