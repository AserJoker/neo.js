#ifndef _H_NEO_COMPILER_NODE_
#define _H_NEO_COMPILER_NODE_

#include "core/allocator.h"
#include "core/location.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum _neo_ast_node_type_t {
  NEO_NODE_TYPE_IDENTIFIER,
  NEO_NODE_TYPE_PRIVATE_NAME,
  NEO_NODE_TYPE_LITERAL_REGEXP,
  NEO_NODE_TYPE_LITERAL_NULL,
  NEO_NODE_TYPE_LITERAL_STRING,
  NEO_NODE_TYPE_LITERAL_BOOLEAN,
  NEO_NODE_TYPE_LITERAL_NUMERIC,
  NEO_NODE_TYPE_LITERAL_BIGINT,
  NEO_NODE_TYPE_LITERAL_DECIMAL,
  NEO_NODE_TYPE_LITERAL_TEMPLATE,
  NEO_NODE_TYPE_PROGRAM,
  NEO_NODE_TYPE_STATEMENT_EXPRESSION,
  NEO_NODE_TYPE_STATEMENT_BLOCK,
  NEO_NODE_TYPE_STATEMENT_EMPTY,
  NEO_NODE_TYPE_STATEMENT_DEBUGGER,
  NEO_NODE_TYPE_STATEMENT_WITH,
  NEO_NODE_TYPE_STATEMENT_RETURN,
  NEO_NODE_TYPE_STATEMENT_LABELED,
  NEO_NODE_TYPE_STATEMENT_BREAK,
  NEO_NODE_TYPE_STATEMENT_CONTINUE,
  NEO_NODE_TYPE_STATEMENT_IF,
  NEO_NODE_TYPE_STATEMENT_SWITCH,
  NEO_NODE_TYPE_STATEMENT_SWITCH_CASE,
  NEO_NODE_TYPE_STATEMENT_THROW,
  NEO_NODE_TYPE_STATEMENT_TRY,
  NEO_NODE_TYPE_STATEMENT_TRY_CATCH,
  NEO_NODE_TYPE_STATEMENT_WHILE,
  NEO_NODE_TYPE_STATEMENT_DO_WHILE,
  NEO_NODE_TYPE_STATEMENT_FOR,
  NEO_NODE_TYPE_STATEMENT_FOR_IN,
  NEO_NODE_TYPE_STATEMENT_FOR_OF,
  NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF,
  NEO_NODE_TYPE_DECLARATION_FUNCTION,
  NEO_NODE_TYPE_DECLARATION_VARIABLE,
  NEO_NODE_TYPE_DECLARATION_VARIABLE_DECLARATOR,
  NEO_NODE_TYPE_DECLARATION_IMPORT,
  NEO_NODE_TYPE_DECLARATION_EXPORT,
  NEO_NODE_TYPE_DECLARATION_EXPORT_DEFAULT,
  NEO_NODE_TYPE_DECLARATION_EXPORT_ALL,
  NEO_NODE_TYPE_DECORATOR,
  NEO_NODE_TYPE_DIRECTIVE,
  NEO_NODE_TYPE_INTERPRETER_DIRECTIVE,
  NEO_NODE_TYPE_EXPRESSION_SUPER,
  NEO_NODE_TYPE_EXPRESSION_THIS,
  NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION,
  NEO_NODE_TYPE_EXPRESSION_YIELD,
  NEO_NODE_TYPE_EXPRESSION_ARRAY,
  NEO_NODE_TYPE_EXPRESSION_OBJECT,
  NEO_NODE_TYPE_EXPRESSION_RECORD,
  NEO_NODE_TYPE_EXPRESSION_TUPLE,
  NEO_NODE_TYPE_EXPRESSION_FUNCTION,
  NEO_NODE_TYPE_EXPRESSION_BINARY,
  NEO_NODE_TYPE_EXPRESSION_ASSIGMENT,
  NEO_NODE_TYPE_EXPRESSION_SPREAD,
  NEO_NODE_TYPE_EXPRESSION_MEMBER,
  NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER,
  NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER,
  NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER,
  NEO_NODE_TYPE_EXPRESSION_CONDITION,
  NEO_NODE_TYPE_EXPRESSION_CALL,
  NEO_NODE_TYPE_EXPRESSION_OPTIONAL_CALL,
  NEO_NODE_TYPE_EXPRESSION_NEW,
  NEO_NODE_TYPE_EXPRESSION_GROUP,
  NEO_NODE_TYPE_PATTERN_OBJECT,
  NEO_NODE_TYPE_PATTERN_ARRAY,
  NEO_NODE_TYPE_PATTERN_REST,
  NEO_NODE_TYPE_PATTERN_ASSIGMENT,
  NEO_NODE_TYPE_PATTERN_CLASS,
  NEO_NODE_TYPE_CLASS_METHOD,
  NEO_NODE_TYPE_CLASS_PROPERTY,
  NEO_NODE_TYPE_CLASS_STATIC_BLOCK,
  NEO_NODE_TYPE_IMPORT_SPECIFIER,
  NEO_NODE_TYPE_IMPORT_DEFAULT_SPECIFIER,
  NEO_NODE_TYPE_IMPORT_NAMESPACE_SPECIFIER,
  NEO_NODE_TYPE_IMPORT_ATTRIBUTE,
  NEO_NODE_TYPE_EXPORT_SPECIFIER,
  NEO_NODE_TYPE_EXPORT_NAMESPACE_SPECIFIER,
} neo_ast_node_type_t;

typedef struct _neo_ast_node_t {
  neo_ast_node_type_t type;
  neo_location_t location;
} *neo_ast_node_t;

bool neo_skip_white_space(neo_allocator_t allocator, const char *file,
                          neo_position_t *position);

bool neo_skip_line_terminator(neo_allocator_t allocator, const char *file,
                              neo_position_t *position);

bool neo_skip_comment(neo_allocator_t allocator, const char *file,
                      neo_position_t *position);
#define SKIP_ALL(allocator, file, position, onerror)                           \
  for (;;) {                                                                   \
    if (neo_skip_white_space(allocator, file, position)) {                     \
      continue;                                                                \
    }                                                                          \
    if (neo_skip_line_terminator(allocator, file, position)) {                 \
      continue;                                                                \
    }                                                                          \
    TRY(if (neo_skip_comment(allocator, file, position)) { continue; }) {      \
      goto onerror;                                                            \
    }                                                                          \
    break;                                                                     \
  }

#ifdef __cplusplus
}
#endif
#endif