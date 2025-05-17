#include "compiler/class_accessor.h"
#include "compiler/class_method.h"
#include "compiler/class_property.h"
#include "compiler/declaration_class.h"
#include "compiler/declaration_function.h"
#include "compiler/declaration_variable.h"
#include "compiler/decorator.h"
#include "compiler/expression.h"
#include "compiler/expression_array.h"
#include "compiler/expression_arrow_function.h"
#include "compiler/expression_assigment.h"
#include "compiler/expression_call.h"
#include "compiler/expression_class.h"
#include "compiler/expression_condition.h"
#include "compiler/expression_function.h"
#include "compiler/expression_member.h"
#include "compiler/expression_new.h"
#include "compiler/expression_object.h"
#include "compiler/expression_spread.h"
#include "compiler/expression_super.h"
#include "compiler/expression_this.h"
#include "compiler/expression_yield.h"
#include "compiler/function_argument.h"
#include "compiler/function_body.h"
#include "compiler/interpreter.h"
#include "compiler/literal_numeric.h"
#include "compiler/literal_string.h"
#include "compiler/literal_template.h"
#include "compiler/node.h"
#include "compiler/object_accessor.h"
#include "compiler/object_method.h"
#include "compiler/object_property.h"
#include "compiler/parser.h"
#include "compiler/pattern_array.h"
#include "compiler/pattern_array_item.h"
#include "compiler/pattern_object.h"
#include "compiler/pattern_object_item.h"
#include "compiler/pattern_rest.h"
#include "compiler/program.h"
#include "compiler/statement_block.h"
#include "compiler/statement_break.h"
#include "compiler/statement_continue.h"
#include "compiler/statement_do_while.h"
#include "compiler/statement_expression.h"
#include "compiler/statement_for.h"
#include "compiler/statement_for_await_of.h"
#include "compiler/statement_for_in.h"
#include "compiler/statement_for_of.h"
#include "compiler/statement_if.h"
#include "compiler/statement_labeled.h"
#include "compiler/statement_return.h"
#include "compiler/statement_switch.h"
#include "compiler/statement_throw.h"
#include "compiler/statement_try.h"
#include "compiler/statement_while.h"
#include "compiler/static_block.h"
#include "compiler/switch_case.h"
#include "compiler/token.h"
#include "compiler/try_catch.h"
#include "compiler/variable_declarator.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include <stdio.h>
#include <string.h>

void print_location(neo_location_t location) {}

#define JSON_START "{"
#define JSON_END "}"
#define JSON_FIELD(name)                                                       \
  "\"" #name "\""                                                              \
  ":"
#define JSON_SPLIT ","

#define JSON_VALUE(value) "\"" value "\""

void print(neo_allocator_t allocator, neo_ast_node_t node);

void print_list(neo_allocator_t allocator, neo_list_t list) {
  neo_list_node_t node = neo_list_get_first(list);
  printf("[");
  while (node && node != neo_list_get_tail(list)) {
    if (node != neo_list_get_first(list)) {
      printf(",");
    }
    print(allocator, (neo_ast_node_t)neo_list_node_get(node));
    node = neo_list_node_next(node);
  }
  printf("]");
}

void print(neo_allocator_t allocator, neo_ast_node_t node) {
  if (!node) {
    printf("null");
    return;
  }
  size_t len = node->location.end.offset - node->location.begin.offset;
  char *tmp = neo_allocator_alloc(allocator, len + 1, NULL);
  tmp[len] = 0;
  strncpy(tmp, node->location.begin.offset, len);
  char *source = neo_allocator_alloc(allocator, len * 2, NULL);
  char *ptr = source;
  for (size_t idx = 0; tmp[idx] != 0; idx++) {
    if (tmp[idx] == '\n') {
      *ptr++ = '\\';
      *ptr++ = 'n';
    } else if (tmp[idx] == '\r') {
      *ptr++ = '\\';
      *ptr++ = 'r';
    } else if (tmp[idx] == '\t') {
      *ptr++ = '\\';
      *ptr++ = 't';
    } else if (tmp[idx] == '\"') {
      *ptr++ = '\\';
      *ptr++ = '"';
    } else {
      *ptr++ = tmp[idx];
    }
  }
  *ptr = 0;
  neo_allocator_free(allocator, tmp);
  switch (node->type) {
  case NEO_NODE_TYPE_PROGRAM: {
    neo_ast_program_t n = (neo_ast_program_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_PROGRAM"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    if (n->interpreter) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(interpreter));
      print(allocator, n->interpreter);
    }
    printf(JSON_SPLIT);
    printf(JSON_FIELD(directives));
    print_list(allocator, n->directives);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print_list(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_EXPRESSION: {
    neo_ast_statement_expression_t n = (neo_ast_statement_expression_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_EXPRESSION"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    if (n->expression) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(expression));
      print(allocator, n->expression);
    }
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_LITERAL_STRING: {
    neo_ast_literal_string_t n = (neo_ast_literal_string_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_LITERAL_STRING"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;

  case NEO_NODE_TYPE_LITERAL_NUMERIC: {
    neo_ast_literal_numeric_t n = (neo_ast_literal_numeric_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_LITERAL_NUMERIC"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_LITERAL_NULL: {
    neo_ast_literal_numeric_t n = (neo_ast_literal_numeric_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_LITERAL_NULL"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_LITERAL_BOOLEAN: {
    neo_ast_literal_numeric_t n = (neo_ast_literal_numeric_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_LITERAL_BOOLEAN"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_LITERAL_BIGINT: {
    neo_ast_literal_numeric_t n = (neo_ast_literal_numeric_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_LITERAL_BIGINT"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_BINARY: {
    neo_ast_expression_binary_t n = (neo_ast_expression_binary_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_BINARY"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    if (n->left) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(left));
      print(allocator, n->left);
    }
    if (n->right) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(right));
      print(allocator, n->right);
    }
    char opt[1024];
    size_t len = n->opt->location.end.offset - n->opt->location.begin.offset;
    opt[len] = 0;
    strncpy(opt, n->opt->location.begin.offset, len);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(operator) JSON_VALUE("%s"), opt);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_DIRECTIVE: {
    neo_ast_expression_binary_t n = (neo_ast_expression_binary_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_DIRECTIVE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_INTERPRETER_DIRECTIVE: {
    neo_ast_interpreter_t n = (neo_ast_interpreter_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_INTERPRETER_DIRECTIVE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_BLOCK: {
    neo_ast_statement_block_t n = (neo_ast_statement_block_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_BLOCK"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print_list(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_EMPTY: {
    neo_ast_interpreter_t n = (neo_ast_interpreter_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_EMPTY"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_DEBUGGER: {
    neo_ast_interpreter_t n = (neo_ast_interpreter_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_DEBUGGER"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_IDENTIFIER: {
    neo_ast_interpreter_t n = (neo_ast_interpreter_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_IDENTIFIER"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_PRIVATE_NAME: {
    neo_ast_interpreter_t n = (neo_ast_interpreter_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_PRIVATE_NAME"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_LITERAL_REGEXP: {
    neo_ast_interpreter_t n = (neo_ast_interpreter_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_LITERAL_REGEXP"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_FUNCTION_ARGUMENT: {
    neo_ast_function_argument_t n = (neo_ast_function_argument_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_FUNCTION_ARGUMENT"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(identifier));
    print(allocator, n->identifier);
    if (n->value) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(value));
      print(allocator, n->value);
    }
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_FUNCTION_BODY: {
    neo_ast_function_body_t n = (neo_ast_function_body_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_FUNCTION_BODY"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(statements));
    print_list(allocator, n->body);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(directives));
    print_list(allocator, n->directives);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION: {
    neo_ast_expression_arrow_function_t n =
        (neo_ast_expression_arrow_function_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type)
               JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(async) JSON_VALUE("%s"), n->async ? "true" : "false");
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_PATTERN_OBJECT: {
    neo_ast_pattern_object_t n = (neo_ast_pattern_object_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_PATTERN_OBJECT"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(items));
    print_list(allocator, n->items);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_PATTERN_ARRAY: {
    neo_ast_pattern_array_t n = (neo_ast_pattern_array_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_PATTERN_ARRAY"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(items));
    print_list(allocator, n->items);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_PATTERN_OBJECT_ITEM: {
    neo_ast_pattern_object_item_t n = (neo_ast_pattern_object_item_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_PATTERN_OBJECT_ITEM"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(identifier));
    print(allocator, n->identifier);
    if (n->alias) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(alias));
      print(allocator, n->alias);
    }
    if (n->value) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(value));
      print(allocator, n->value);
    }
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_PATTERN_ARRAY_ITEM: {
    neo_ast_pattern_array_item_t n = (neo_ast_pattern_array_item_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_PATTERN_ARRAY_ITEM"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);

    printf(JSON_SPLIT);
    printf(JSON_FIELD(identifier));
    print(allocator, n->identifier);
    if (n->value) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(value));
      print(allocator, n->value);
    }
    printf(JSON_END);
  } break;

  case NEO_NODE_TYPE_PATTERN_REST: {
    neo_ast_pattern_rest_t n = (neo_ast_pattern_rest_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_PATTERN_REST"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(identifier));
    print(allocator, n->identifier);
    printf(JSON_END);
  } break;

  case NEO_NODE_TYPE_EXPRESSION_ASSIGMENT: {
    neo_ast_expression_assigment_t n = (neo_ast_expression_assigment_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_ASSIGMENT"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(identifier));
    print(allocator, n->identifier);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(value));
    print(allocator, n->value);
    char opt[1024];
    size_t len = n->opt->location.end.offset - n->opt->location.begin.offset;
    opt[len] = 0;
    strncpy(opt, n->opt->location.begin.offset, len);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(opt) JSON_VALUE("%s"), opt);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_CONDITION: {
    neo_ast_expression_condition_t n = (neo_ast_expression_condition_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_CONDITION"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(condition));
    print(allocator, n->condition);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(alternate));
    print(allocator, n->alternate);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(consequent));
    print(allocator, n->consequent);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_MEMBER: {
    neo_ast_expression_member_t n = (neo_ast_expression_member_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_MEMBER"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(host));
    print(allocator, n->host);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(field));
    print(allocator, n->field);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER: {
    neo_ast_expression_member_t n = (neo_ast_expression_member_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type)
               JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(host));
    print(allocator, n->host);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(field));
    print(allocator, n->field);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER: {
    neo_ast_expression_member_t n = (neo_ast_expression_member_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type)
               JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(host));
    print(allocator, n->host);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(field));
    print(allocator, n->field);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER: {
    neo_ast_expression_member_t n = (neo_ast_expression_member_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type)
               JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(host));
    print(allocator, n->host);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(field));
    print(allocator, n->field);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_CALL: {
    neo_ast_expression_call_t n = (neo_ast_expression_call_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_CALL"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(callee));
    print(allocator, n->callee);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_OPTIONAL_CALL: {
    neo_ast_expression_call_t n = (neo_ast_expression_call_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type)
               JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_OPTIONAL_CALL"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(host));
    print(allocator, n->callee);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_LITERAL_TEMPLATE: {
    neo_ast_literal_template_t n = (neo_ast_literal_template_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_LITERAL_TEMPLATE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    if (n->tag) {
      printf(JSON_SPLIT);
      printf(JSON_FIELD(tag));
      print(allocator, n->tag);
    }
    printf(JSON_SPLIT);
    printf(JSON_FIELD(quasis));
    neo_list_node_t it = neo_list_get_first(n->quasis);
    printf("[");
    while (it != neo_list_get_tail(n->quasis)) {
      if (it != neo_list_get_first(n->quasis)) {
        printf(",");
      }
      neo_token_t token = neo_list_node_get(it);
      char value[1024];
      size_t len = token->location.end.offset - token->location.begin.offset;
      char *pstr = &value[0];
      for (size_t idx = 0; idx < len; idx++) {
        if (token->location.begin.offset[idx] == '\n') {
          *ptr++ = '\\';
          *ptr++ = 'n';
        } else if (token->location.begin.offset[idx] == '\r') {
          *ptr++ = '\\';
          *ptr++ = 'r';
        } else {
          *pstr++ = token->location.begin.offset[idx];
        }
      }
      *pstr = 0;
      it = neo_list_node_next(it);
      printf(JSON_VALUE("%s"), value);
    }
    printf("]");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(expressions));
    print_list(allocator, n->expressions);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_NEW: {
    neo_ast_expression_new_t n = (neo_ast_expression_new_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_NEW"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(callee));
    print(allocator, n->callee);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_YIELD: {
    neo_ast_expression_yield_t n = (neo_ast_expression_yield_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_YIELD"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(value));
    print(allocator, n->value);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(degelate));
    printf(JSON_VALUE("%s"), n->degelate ? "true" : "false");
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_SPREAD: {
    neo_ast_expression_spread_t n = (neo_ast_expression_spread_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_SPREAD"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(value));
    print(allocator, n->value);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_ARRAY: {
    neo_ast_expression_array_t n = (neo_ast_expression_array_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_ARRAY"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(items));
    print_list(allocator, n->items);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_FUNCTION: {
    neo_ast_expression_function_t n = (neo_ast_expression_function_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_FUNCTION"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(name));
    print(allocator, n->name);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(async));
    printf(JSON_VALUE("%s"), n->async ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(generator));
    printf(JSON_VALUE("%s"), n->generator ? "true" : "false");
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_OBJECT: {
    neo_ast_expression_object_t n = (neo_ast_expression_object_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_OBJECT"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(items));
    print_list(allocator, n->items);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_OBJECT_PROPERTY: {
    neo_ast_object_property_t n = (neo_ast_object_property_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_OBJECT_PROPERTY"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(identifier));
    print(allocator, n->identifier);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(value));
    print(allocator, n->value);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(computed));
    printf(JSON_VALUE("%s"), n->computed ? "true" : "false");
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_OBJECT_METHOD: {
    neo_ast_object_method_t n = (neo_ast_object_method_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_OBJECT_METHOD"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(name));
    print(allocator, n->name);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(computed));
    printf(JSON_VALUE("%s"), n->computed ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(async));
    printf(JSON_VALUE("%s"), n->async ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(generator));
    printf(JSON_VALUE("%s"), n->generator ? "true" : "false");
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_OBJECT_ACCESSOR: {
    neo_ast_object_accessor_t n = (neo_ast_object_accessor_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_OBJECT_ACCESSOR"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(name));
    print(allocator, n->name);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(computed));
    printf(JSON_VALUE("%s"), n->computed ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(kind));
    printf(JSON_VALUE("%s"), n->kind == NEO_ACCESSOR_KIND_GET ? "GET" : "SET");
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_SUPER: {
    neo_ast_expression_this_t n = (neo_ast_expression_this_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_SUPER"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_THIS: {
    neo_ast_expression_super_t n = (neo_ast_expression_super_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_THIS"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_CLASS: {
    neo_ast_expression_class_t n = (neo_ast_expression_class_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_EXPRESSION_CLASS"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(name));
    print(allocator, n->name);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(extends));
    print(allocator, n->extends);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(items));
    print_list(allocator, n->items);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(decorators));
    print_list(allocator, n->decorators);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_CLASS_METHOD: {
    neo_ast_class_method_t n = (neo_ast_class_method_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_CLASS_METHOD"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(name));
    print(allocator, n->name);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(computed));
    printf(JSON_VALUE("%s"), n->computed ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(async));
    printf(JSON_VALUE("%s"), n->async ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(generator));
    printf(JSON_VALUE("%s"), n->generator ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(static));
    printf(JSON_VALUE("%s"), n->static_ ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(decorators));
    print_list(allocator, n->decorators);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_CLASS_ACCESSOR: {
    neo_ast_class_accessor_t n = (neo_ast_class_accessor_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_CLASS_ACCESSOR"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(name));
    print(allocator, n->name);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(computed));
    printf(JSON_VALUE("%s"), n->computed ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(kind));
    printf(JSON_VALUE("%s"), n->kind == NEO_ACCESSOR_KIND_GET ? "get" : "set");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(static));
    printf(JSON_VALUE("%s"), n->static_ ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(decorators));
    print_list(allocator, n->decorators);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_CLASS_PROPERTY: {
    neo_ast_class_property_t n = (neo_ast_class_property_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_CLASS_PROPERTY"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(identifier));
    print(allocator, n->identifier);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(value));
    print(allocator, n->value);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(computed));
    printf(JSON_VALUE("%s"), n->computed ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(static));
    printf(JSON_VALUE("%s"), n->static_ ? "true" : "false");
    printf(JSON_SPLIT);
    printf(JSON_FIELD(decorators));
    print_list(allocator, n->decorators);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATIC_BLOCK: {
    neo_ast_static_block_t n = (neo_ast_static_block_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATIC_BLOCK"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print_list(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_DECORATOR: {
    neo_ast_decorator_t n = (neo_ast_decorator_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_DECORATOR"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(callee));
    print(allocator, n->callee);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(arguments));
    print_list(allocator, n->arguments);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_RETURN: {
    neo_ast_statement_return_t n = (neo_ast_statement_return_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_RETURN"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(value));
    print(allocator, n->value);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_BREAK: {
    neo_ast_statement_break_t n = (neo_ast_statement_break_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_BREAK"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(label));
    print(allocator, n->label);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_CONTINUE: {
    neo_ast_statement_continue_t n = (neo_ast_statement_continue_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_CONTINUE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(label));
    print(allocator, n->label);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_LABELED: {
    neo_ast_statement_labeled_t n = (neo_ast_statement_labeled_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_CONTINUE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(label));
    print(allocator, n->label);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(statement));
    print(allocator, n->statement);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_IF: {
    neo_ast_statement_if_t n = (neo_ast_statement_if_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_IF"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(condition));
    print(allocator, n->condition);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(alternate));
    print(allocator, n->alternate);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(consequent));
    print(allocator, n->consequent);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_SWITCH: {
    neo_ast_statement_switch_t n = (neo_ast_statement_switch_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_SWITCH"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(condition));
    print(allocator, n->condition);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(cases));
    print_list(allocator, n->cases);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_SWITCH_CASE: {
    neo_ast_switch_case_t n = (neo_ast_switch_case_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_SWITCH_CASE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(condition));
    print(allocator, n->condition);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print_list(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_THROW: {
    neo_ast_statement_throw_t n = (neo_ast_statement_throw_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_THROW"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(value));
    print(allocator, n->value);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_TRY: {
    neo_ast_statement_try_t n = (neo_ast_statement_try_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_TRY"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(catch));
    print(allocator, n->catch);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(finally));
    print(allocator, n->finally);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_TRY_CATCH: {
    neo_ast_try_catch_t n = (neo_ast_try_catch_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_TRY_CATCH"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(error));
    print(allocator, n->error);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_WHILE: {
    neo_ast_statement_while_t n = (neo_ast_statement_while_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_WHILE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(condition));
    print(allocator, n->condition);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_DO_WHILE: {
    neo_ast_statement_do_while_t n = (neo_ast_statement_do_while_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_DO_WHILE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(condition));
    print(allocator, n->condition);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_FOR: {
    neo_ast_statement_for_t n = (neo_ast_statement_for_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_FOR"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(initialize));
    print(allocator, n->initialize);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(condition));
    print(allocator, n->condition);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(after));
    print(allocator, n->after);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_FOR_IN: {
    neo_ast_statement_for_in_t n = (neo_ast_statement_for_in_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_FOR_IN"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(left));
    print(allocator, n->left);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(right));
    print(allocator, n->right);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(kind));
    if (n->kind == NEO_AST_DECLARATION_VAR) {
      printf(JSON_VALUE("%s"), "var");
    } else if (n->kind == NEO_AST_DECLARATION_CONST) {
      printf(JSON_VALUE("%s"), "const");
    } else if (n->kind == NEO_AST_DECLARATION_LET) {
      printf(JSON_VALUE("%s"), "let");
    } else if (n->kind == NEO_AST_DECLARATION_NONE) {
      printf(JSON_VALUE("%s"), "none");
    }
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_FOR_OF: {
    neo_ast_statement_for_of_t n = (neo_ast_statement_for_of_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_FOR_OF"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(left));
    print(allocator, n->left);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(right));
    print(allocator, n->right);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(kind));
    if (n->kind == NEO_AST_DECLARATION_VAR) {
      printf(JSON_VALUE("%s"), "var");
    } else if (n->kind == NEO_AST_DECLARATION_CONST) {
      printf(JSON_VALUE("%s"), "const");
    } else if (n->kind == NEO_AST_DECLARATION_LET) {
      printf(JSON_VALUE("%s"), "let");
    } else if (n->kind == NEO_AST_DECLARATION_NONE) {
      printf(JSON_VALUE("%s"), "none");
    }
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF: {
    neo_ast_statement_for_await_of_t n = (neo_ast_statement_for_await_of_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(left));
    print(allocator, n->left);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(right));
    print(allocator, n->right);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(kind));
    if (n->kind == NEO_AST_DECLARATION_VAR) {
      printf(JSON_VALUE("%s"), "var");
    } else if (n->kind == NEO_AST_DECLARATION_CONST) {
      printf(JSON_VALUE("%s"), "const");
    } else if (n->kind == NEO_AST_DECLARATION_LET) {
      printf(JSON_VALUE("%s"), "let");
    } else if (n->kind == NEO_AST_DECLARATION_NONE) {
      printf(JSON_VALUE("%s"), "none");
    }
    printf(JSON_SPLIT);
    printf(JSON_FIELD(body));
    print(allocator, n->body);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_DECLARATION_CLASS: {
    neo_ast_declaration_class_t n = (neo_ast_declaration_class_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_DECLARATION_CLASS"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(declaration));
    print(allocator, n->declaration);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_DECLARATION_FUNCTION: {
    neo_ast_declaration_function_t n = (neo_ast_declaration_function_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_DECLARATION_FUNCTION"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(declaration));
    print(allocator, n->declaration);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_DECLARATION_VARIABLE: {
    neo_ast_declaration_variable_t n = (neo_ast_declaration_variable_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_DECLARATION_VARIABLE"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(declarators));
    print_list(allocator, n->declarators);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(kind));
    if (n->kind == NEO_AST_DECLARATION_VAR) {
      printf(JSON_VALUE("%s"), "var");
    } else if (n->kind == NEO_AST_DECLARATION_CONST) {
      printf(JSON_VALUE("%s"), "const");
    } else if (n->kind == NEO_AST_DECLARATION_LET) {
      printf(JSON_VALUE("%s"), "let");
    }
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_VARIABLE_DECLARATOR: {
    neo_ast_variable_declarator_t n = (neo_ast_variable_declarator_t)node;
    printf(JSON_START);
    printf(JSON_FIELD(type) JSON_VALUE("NEO_NODE_TYPE_VARIABLE_DECLARATOR"));
    printf(JSON_SPLIT);
    printf(JSON_FIELD(source) JSON_VALUE("%s"), source);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(identifier));
    print(allocator, n->identifier);
    printf(JSON_SPLIT);
    printf(JSON_FIELD(initialize));
    print(allocator, n->initialize);
    printf(JSON_END);
  } break;
  case NEO_NODE_TYPE_DECLARATION_IMPORT:
  case NEO_NODE_TYPE_DECLARATION_EXPORT:
  case NEO_NODE_TYPE_DECLARATION_EXPORT_DEFAULT:
  case NEO_NODE_TYPE_DECLARATION_EXPORT_ALL:
  case NEO_NODE_TYPE_IMPORT_SPECIFIER:
  case NEO_NODE_TYPE_IMPORT_DEFAULT_SPECIFIER:
  case NEO_NODE_TYPE_IMPORT_NAMESPACE_SPECIFIER:
  case NEO_NODE_TYPE_IMPORT_ATTRIBUTE:
  case NEO_NODE_TYPE_EXPORT_SPECIFIER:
  case NEO_NODE_TYPE_EXPORT_NAMESPACE_SPECIFIER:
    break;
  }
  neo_allocator_free(allocator, source);
}

int main(int argc, char *argv[]) {
  if (argc > 1) {
    neo_allocator_t allocator = neo_create_default_allocator();
    neo_error_initialize(allocator);
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
      fprintf(stderr, "cannot open file: %s\n", argv[1]);
      return -1;
    }
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    char *buf = (char *)neo_allocator_alloc(allocator, size + 1, NULL);
    memset(buf, 0, size + 1);
    fseek(fp, 0, SEEK_SET);
    fread(buf, size, 1, fp);
    fclose(fp);
    neo_ast_node_t node = neo_ast_parse_code(allocator, argv[1], buf);
    if (neo_has_error()) {
      neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
      char *msg = neo_error_to_string(error);
      fprintf(stderr, "%s\n", msg);
      neo_allocator_free(allocator, msg);
      neo_allocator_free(allocator, error);
    } else {
      print(allocator, node);
      printf("\n");
    }
    neo_allocator_free(allocator, node);
    neo_allocator_free(allocator, buf);
    neo_delete_allocator(allocator);
  }
  return 0;
}