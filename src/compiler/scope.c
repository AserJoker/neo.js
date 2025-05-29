#include "compiler/scope.h"
#include "compiler/class_accessor.h"
#include "compiler/class_method.h"
#include "compiler/class_property.h"
#include "compiler/declaration_class.h"
#include "compiler/declaration_export.h"
#include "compiler/declaration_function.h"
#include "compiler/declaration_variable.h"
#include "compiler/decorator.h"
#include "compiler/export_default.h"
#include "compiler/export_specifier.h"
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
#include "compiler/expression_yield.h"
#include "compiler/function_argument.h"
#include "compiler/function_body.h"
#include "compiler/literal_template.h"
#include "compiler/node.h"
#include "compiler/object_accessor.h"
#include "compiler/object_method.h"
#include "compiler/object_property.h"
#include "compiler/pattern_array.h"
#include "compiler/pattern_array_item.h"
#include "compiler/pattern_object.h"
#include "compiler/pattern_object_item.h"
#include "compiler/pattern_rest.h"
#include "compiler/program.h"
#include "compiler/statement_block.h"
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
#include "compiler/try_catch.h"
#include "compiler/variable_declarator.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/variable.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

neo_compile_scope_t current = NULL;

static void neo_compile_scope_dispose(neo_allocator_t allocator,
                                      neo_compile_scope_t scope) {
  neo_allocator_free(allocator, scope->variables);
}

static neo_compile_scope_t neo_create_compile_scope(neo_allocator_t allocator) {
  neo_compile_scope_t scope = (neo_compile_scope_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_compile_scope_t),
      neo_compile_scope_dispose);
  scope->parent = NULL;
  neo_list_initialize_t initialize = {true};
  scope->variables = neo_create_list(allocator, &initialize);
  scope->type = NEO_COMPILE_SCOPE_FUNCTION;
  return scope;
}

neo_compile_scope_t neo_compile_scope_push(neo_allocator_t allocator,
                                           neo_compile_scope_type_t type) {
  neo_compile_scope_t scope = neo_create_compile_scope(allocator);
  scope->parent = current;
  scope->type = type;
  neo_compile_scope_t curr = current;
  current = scope;
  return curr;
}

neo_compile_scope_t neo_compile_scope_pop(neo_compile_scope_t scope) {
  neo_compile_scope_t curr = current;
  current = scope;
  return curr;
}

static void neo_compile_variable_dispose(neo_allocator_t allocator,
                                         neo_compile_variable_t variable) {
  neo_allocator_free(allocator, variable->name);
}

static neo_compile_variable_t
neo_create_compile_variable(neo_allocator_t allocator) {
  neo_compile_variable_t variable = (neo_compile_variable_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_compile_variable_t),
      neo_compile_variable_dispose);
  variable->name = NULL;
  variable->type = NEO_COMPILE_VARIABLE_VAR;
  return variable;
}

void neo_compile_scope_declar_value(neo_allocator_t allocator,
                                    neo_compile_scope_t self, const char *name,
                                    neo_compile_variable_type_t type) {
  size_t len = strlen(name);
  neo_compile_variable_t variable = neo_create_compile_variable(allocator);
  variable->type = type;
  variable->name = neo_allocator_alloc(allocator, len + 1, NULL);
  strcpy(variable->name, name);
  variable->name[len] = 0;
  if (type == NEO_COMPILE_VARIABLE_VAR ||
      type == NEO_COMPILE_VARIABLE_FUNCTION) {
    while (self->type == NEO_COMPILE_SCOPE_BLOCK) {
      self = self->parent;
    }
  }
  neo_list_push(self->variables, variable);
}
void neo_compile_scope_declar(neo_allocator_t allocator,
                              neo_compile_scope_t self, neo_ast_node_t node,
                              neo_compile_variable_type_t type) {
  if (node->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, node->location);
    neo_compile_scope_declar_value(allocator, self, name, type);
    neo_allocator_free(allocator, name);

  } else if (node->type == NEO_NODE_TYPE_FUNCTION_ARGUMENT) {
    neo_ast_function_argument_t argument = (neo_ast_function_argument_t)node;
    neo_compile_scope_declar(allocator, self, argument->identifier,
                             NEO_COMPILE_VARIABLE_LET);
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_FUNCTION) {
    neo_ast_expression_function_t function =
        (neo_ast_expression_function_t)node;
    if (function->name) {
      neo_compile_scope_declar(allocator, self, function->name,
                               NEO_COMPILE_VARIABLE_FUNCTION);
    }
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_CLASS) {
    neo_ast_expression_class_t clazz = (neo_ast_expression_class_t)node;
    if (clazz->name) {
      neo_compile_scope_declar(allocator, self, clazz->name, type);
    }
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_CLASS) {
    neo_ast_expression_class_t clazz = (neo_ast_expression_class_t)node;
    if (clazz->name) {
      neo_compile_scope_declar(allocator, self, clazz->name, type);
    }
  } else if (node->type == NEO_NODE_TYPE_PATTERN_REST) {
    neo_ast_pattern_rest_t rest = (neo_ast_pattern_rest_t)node;
    neo_compile_scope_declar(allocator, self, rest->identifier, type);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_ARRAY_ITEM) {
    neo_ast_pattern_array_item_t array = (neo_ast_pattern_array_item_t)node;
    neo_compile_scope_declar(allocator, self, array->identifier, type);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_OBJECT_ITEM) {
    neo_ast_pattern_object_item_t object = (neo_ast_pattern_object_item_t)node;
    if (object->alias) {
      neo_compile_scope_declar(allocator, self, object->alias, type);
    } else {
      neo_compile_scope_declar(allocator, self, object->identifier, type);
    }
  } else if (node->type == NEO_NODE_TYPE_VARIABLE_DECLARATOR) {
    neo_ast_variable_declarator_t variable =
        (neo_ast_variable_declarator_t)node;
    neo_compile_scope_declar(allocator, self, variable->identifier, type);
  } else if (node->type == NEO_NODE_TYPE_FUNCTION_ARGUMENT) {
    neo_ast_function_argument_t argument = (neo_ast_function_argument_t)node;
    neo_compile_scope_declar(allocator, self, argument->identifier, type);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_OBJECT) {
    neo_ast_pattern_object_t object = (neo_ast_pattern_object_t)node;
    for (neo_list_node_t it = neo_list_get_first(object->items);
         it != neo_list_get_tail(object->items); it = neo_list_node_next(it)) {
      neo_compile_scope_declar(allocator, self, neo_list_node_get(it), type);
    }
  } else if (node->type == NEO_NODE_TYPE_PATTERN_ARRAY) {
    neo_ast_pattern_object_t object = (neo_ast_pattern_object_t)node;
    for (neo_list_node_t it = neo_list_get_first(object->items);
         it != neo_list_get_tail(object->items); it = neo_list_node_next(it)) {
      neo_compile_scope_declar(allocator, self, neo_list_node_get(it), type);
    }
  } else {
    THROW("SyntaxError",
          "Illegal property in declaration context\n  at %s:%d:%d",
          node->location.file, node->location.begin.line,
          node->location.begin.column);
    return;
  }
}
neo_compile_scope_t neo_compile_scope_get_current() { return current; }

static neo_variable_t
neo_serialize_compile_variable(neo_allocator_t allocator,
                               neo_compile_variable_t val) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "name",
                   neo_create_variable_string(allocator, val->name));
  switch (val->type) {
  case NEO_COMPILE_VARIABLE_VAR:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_VARIABLE_VAR"));
    break;
  case NEO_COMPILE_VARIABLE_LET:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_VARIABLE_LET"));
    break;
  case NEO_COMPILE_VARIABLE_CONST:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_VARIABLE_CONST"));
    break;
  case NEO_COMPILE_VARIABLE_FUNCTION:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_VARIABLE_FUNCTION"));
    break;
  }
  return variable;
}

neo_variable_t neo_serialize_scope(neo_allocator_t allocator,
                                   neo_compile_scope_t scope) {
  if (!scope) {
    return neo_create_variable_nil(allocator);
  }
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  switch (scope->type) {
  case NEO_COMPILE_SCOPE_BLOCK:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_SCOPE_BLOCK"));
    break;
  case NEO_COMPILE_SCOPE_FUNCTION:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_SCOPE_FUNCTION"));
    break;
  }
  neo_variable_set(variable, "variables",
                   neo_create_variable_array(
                       allocator, scope->variables,
                       (neo_serialize_fn)neo_serialize_compile_variable));
  return variable;
}

void neo_resolve_closure(neo_allocator_t allocator, neo_ast_node_t node,
                         neo_list_t closure) {
  if (!node) {
    return;
  }
  switch (node->type) {
  case NEO_NODE_TYPE_IDENTIFIER: {
    neo_compile_scope_t scope = neo_compile_scope_get_current();
    char *name = neo_location_get(allocator, node->location);
    for (;;) {
      for (neo_list_node_t it = neo_list_get_first(scope->variables);
           it != neo_list_get_tail(scope->variables);
           it = neo_list_node_next(it)) {
        neo_compile_variable_t variable = neo_list_node_get(it);
        if (strcmp(variable->name, name) == 0) {
          neo_allocator_free(allocator, name);
          return;
        }
      }
      if (scope->type == NEO_COMPILE_SCOPE_FUNCTION) {
        break;
      }
      scope = scope->parent;
    }
    neo_allocator_free(allocator, name);
    neo_list_push(closure, node);
  } break;
  case NEO_NODE_TYPE_PRIVATE_NAME:
  case NEO_NODE_TYPE_LITERAL_REGEXP:
  case NEO_NODE_TYPE_LITERAL_NULL:
  case NEO_NODE_TYPE_LITERAL_STRING:
  case NEO_NODE_TYPE_LITERAL_BOOLEAN:
  case NEO_NODE_TYPE_LITERAL_NUMERIC:
  case NEO_NODE_TYPE_LITERAL_BIGINT:
    break;
  case NEO_NODE_TYPE_LITERAL_TEMPLATE: {
    neo_ast_literal_template_t temp = (neo_ast_literal_template_t)node;
    neo_resolve_closure(allocator, temp->tag, closure);
    for (neo_list_node_t it = neo_list_get_first(temp->expressions);
         it != neo_list_get_tail(temp->expressions);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_PROGRAM: {
    neo_ast_program_t program = (neo_ast_program_t)node;
    for (neo_list_node_t it = neo_list_get_first(program->body);
         it != neo_list_get_tail(program->body); it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_STATEMENT_EXPRESSION: {
    neo_ast_statement_expression_t statement =
        (neo_ast_statement_expression_t)node;
    neo_resolve_closure(allocator, statement->expression, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_BLOCK: {
    neo_ast_statement_block_t statement = (neo_ast_statement_block_t)node;
    for (neo_list_node_t it = neo_list_get_first(statement->body);
         it != neo_list_get_tail(statement->body);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_STATEMENT_EMPTY:
  case NEO_NODE_TYPE_STATEMENT_DEBUGGER:
    break;
  case NEO_NODE_TYPE_STATEMENT_RETURN: {
    neo_ast_statement_return_t statement = (neo_ast_statement_return_t)node;
    neo_resolve_closure(allocator, statement->value, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_LABELED: {
    neo_ast_statement_labeled_t statement = (neo_ast_statement_labeled_t)node;
    neo_resolve_closure(allocator, statement->statement, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_BREAK:
  case NEO_NODE_TYPE_STATEMENT_CONTINUE:
    break;
  case NEO_NODE_TYPE_STATEMENT_IF: {
    neo_ast_statement_if_t statement = (neo_ast_statement_if_t)node;
    neo_resolve_closure(allocator, statement->condition, closure);
    neo_resolve_closure(allocator, statement->consequent, closure);
    neo_resolve_closure(allocator, statement->alternate, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_SWITCH: {
    neo_ast_statement_switch_t statement = (neo_ast_statement_switch_t)node;
    neo_resolve_closure(allocator, statement->condition, closure);
    for (neo_list_node_t it = neo_list_get_first(statement->cases);
         it != neo_list_get_tail(statement->cases);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_SWITCH_CASE: {
    neo_ast_switch_case_t cas = (neo_ast_switch_case_t)node;
    neo_resolve_closure(allocator, cas->condition, closure);
    for (neo_list_node_t it = neo_list_get_first(cas->body);
         it != neo_list_get_tail(cas->body); it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_STATEMENT_THROW: {
    neo_ast_statement_throw_t statement = (neo_ast_statement_throw_t)node;
    neo_resolve_closure(allocator, statement->value, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_TRY: {
    neo_ast_statement_try_t statement = (neo_ast_statement_try_t)node;
    neo_resolve_closure(allocator, statement->body, closure);
    neo_resolve_closure(allocator, statement->catch, closure);
    neo_resolve_closure(allocator, statement->finally, closure);
  } break;
  case NEO_NODE_TYPE_TRY_CATCH: {
    neo_ast_try_catch_t trycatch = (neo_ast_try_catch_t)node;
    neo_resolve_closure(allocator, trycatch->body, closure);
    if (trycatch->error && trycatch->error->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, trycatch->error, closure);
    }
  } break;
  case NEO_NODE_TYPE_STATEMENT_WHILE: {
    neo_ast_statement_while_t statement = (neo_ast_statement_while_t)node;
    neo_resolve_closure(allocator, statement->condition, closure);
    neo_resolve_closure(allocator, statement->body, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_DO_WHILE: {
    neo_ast_statement_while_t statement = (neo_ast_statement_while_t)node;
    neo_resolve_closure(allocator, statement->condition, closure);
    neo_resolve_closure(allocator, statement->body, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_FOR: {
    neo_ast_statement_for_t statement = (neo_ast_statement_for_t)node;
    neo_resolve_closure(allocator, statement->condition, closure);
    neo_resolve_closure(allocator, statement->body, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_FOR_IN: {
    neo_ast_statement_for_in_t statement = (neo_ast_statement_for_in_t)node;
    if (statement->left->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, statement->left, closure);
    }
    neo_resolve_closure(allocator, statement->right, closure);
    neo_resolve_closure(allocator, statement->body, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_FOR_OF: {
    neo_ast_statement_for_of_t statement = (neo_ast_statement_for_of_t)node;
    if (statement->left->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, statement->left, closure);
    }
    neo_resolve_closure(allocator, statement->right, closure);
    neo_resolve_closure(allocator, statement->body, closure);
  } break;
  case NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF: {
    neo_ast_statement_for_await_of_t statement =
        (neo_ast_statement_for_await_of_t)node;
    if (statement->left->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, statement->left, closure);
    }
    neo_resolve_closure(allocator, statement->right, closure);
    neo_resolve_closure(allocator, statement->body, closure);
  } break;
  case NEO_NODE_TYPE_DECLARATION_FUNCTION: {
    neo_ast_declaration_function_t declaration =
        (neo_ast_declaration_function_t)node;
    neo_resolve_closure(allocator, declaration->declaration, closure);
  } break;
  case NEO_NODE_TYPE_DECLARATION_CLASS: {
    neo_ast_declaration_class_t declaration = (neo_ast_declaration_class_t)node;
    neo_resolve_closure(allocator, declaration->declaration, closure);
  } break;
  case NEO_NODE_TYPE_DECLARATION_VARIABLE: {
    neo_ast_declaration_variable_t declaration =
        (neo_ast_declaration_variable_t)node;
    for (neo_list_node_t it = neo_list_get_first(declaration->declarators);
         it != neo_list_get_tail(declaration->declarators);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_VARIABLE_DECLARATOR: {
    neo_ast_variable_declarator_t declaration =
        (neo_ast_variable_declarator_t)node;
    neo_resolve_closure(allocator, declaration->initialize, closure);
    if (declaration->identifier->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, declaration->identifier, closure);
    }
  } break;
  case NEO_NODE_TYPE_DECLARATION_IMPORT:
    break;
  case NEO_NODE_TYPE_DECLARATION_EXPORT: {
    neo_ast_declaration_export_t declaration =
        (neo_ast_declaration_export_t)node;
    if (!declaration->source) {
      for (neo_list_node_t it = neo_list_get_first(declaration->specifiers);
           it != neo_list_get_tail(declaration->specifiers);
           it = neo_list_node_next(it)) {
        neo_resolve_closure(allocator, neo_list_node_get(it), closure);
      }
    }
  } break;
  case NEO_NODE_TYPE_EXPORT_ALL:
    break;
  case NEO_NODE_TYPE_DECORATOR: {
    neo_ast_decorator_t decorator = (neo_ast_decorator_t)node;
    neo_resolve_closure(allocator, decorator->callee, closure);
    for (neo_list_node_t it = neo_list_get_first(decorator->arguments);
         it != neo_list_get_tail(decorator->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_DIRECTIVE:
    break;
  case NEO_NODE_TYPE_INTERPRETER_DIRECTIVE:
    break;
  case NEO_NODE_TYPE_EXPRESSION_SUPER:
    break;
  case NEO_NODE_TYPE_EXPRESSION_THIS:
    break;
  case NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION: {
    neo_ast_expression_arrow_function_t function =
        (neo_ast_expression_arrow_function_t)node;
    for (neo_list_node_t it = neo_list_get_first(function->arguments);
         it != neo_list_get_tail(function->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->closure);
         it != neo_list_get_tail(function->closure);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_EXPRESSION_YIELD: {
    neo_ast_expression_yield_t expression = (neo_ast_expression_yield_t)node;
    neo_resolve_closure(allocator, expression->value, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_ARRAY: {
    neo_ast_expression_array_t array = (neo_ast_expression_array_t)node;
    for (neo_list_node_t it = neo_list_get_first(array->items);
         it != neo_list_get_tail(array->items); it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_EXPRESSION_OBJECT: {
    neo_ast_expression_object_t object = (neo_ast_expression_object_t)node;
    for (neo_list_node_t it = neo_list_get_first(object->items);
         it != neo_list_get_tail(object->items); it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_EXPRESSION_FUNCTION: {
    neo_ast_expression_function_t function =
        (neo_ast_expression_function_t)node;
    for (neo_list_node_t it = neo_list_get_first(function->arguments);
         it != neo_list_get_tail(function->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->closure);
         it != neo_list_get_tail(function->closure);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_EXPRESSION_CLASS: {
    neo_ast_expression_class_t clazz = (neo_ast_expression_class_t)node;
    neo_resolve_closure(allocator, clazz->extends, closure);
    for (neo_list_node_t it = neo_list_get_first(clazz->decorators);
         it != neo_list_get_tail(clazz->decorators);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(clazz->items);
         it != neo_list_get_tail(clazz->items); it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_EXPRESSION_BINARY: {
    neo_ast_expression_binary_t expression = (neo_ast_expression_binary_t)node;
    neo_resolve_closure(allocator, expression->left, closure);
    neo_resolve_closure(allocator, expression->right, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_ASSIGMENT: {
    neo_ast_expression_assigment_t expression =
        (neo_ast_expression_assigment_t)node;
    neo_resolve_closure(allocator, expression->identifier, closure);
    neo_resolve_closure(allocator, expression->value, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_SPREAD: {
    neo_ast_expression_spread_t expression = (neo_ast_expression_spread_t)node;
    neo_resolve_closure(allocator, expression->value, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_MEMBER: {
    neo_ast_expression_member_t expression = (neo_ast_expression_member_t)node;
    neo_resolve_closure(allocator, expression->host, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER: {
    neo_ast_expression_member_t expression = (neo_ast_expression_member_t)node;
    neo_resolve_closure(allocator, expression->host, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER: {
    neo_ast_expression_member_t expression = (neo_ast_expression_member_t)node;
    neo_resolve_closure(allocator, expression->host, closure);
    neo_resolve_closure(allocator, expression->field, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER: {
    neo_ast_expression_member_t expression = (neo_ast_expression_member_t)node;
    neo_resolve_closure(allocator, expression->host, closure);
    neo_resolve_closure(allocator, expression->field, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_CONDITION: {
    neo_ast_expression_condition_t expression =
        (neo_ast_expression_condition_t)node;
    neo_resolve_closure(allocator, expression->condition, closure);
    neo_resolve_closure(allocator, expression->consequent, closure);
    neo_resolve_closure(allocator, expression->alternate, closure);
  } break;
  case NEO_NODE_TYPE_EXPRESSION_CALL: {
    neo_ast_expression_call_t expression = (neo_ast_expression_call_t)node;
    neo_resolve_closure(allocator, expression->callee, closure);
    for (neo_list_node_t it = neo_list_get_first(expression->arguments);
         it != neo_list_get_tail(expression->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_EXPRESSION_OPTIONAL_CALL: {
    neo_ast_expression_call_t expression = (neo_ast_expression_call_t)node;
    neo_resolve_closure(allocator, expression->callee, closure);
    for (neo_list_node_t it = neo_list_get_first(expression->arguments);
         it != neo_list_get_tail(expression->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_EXPRESSION_NEW: {
    neo_ast_expression_new_t expression = (neo_ast_expression_new_t)node;
    neo_resolve_closure(allocator, expression->callee, closure);
    for (neo_list_node_t it = neo_list_get_first(expression->arguments);
         it != neo_list_get_tail(expression->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_PATTERN_OBJECT: {
    neo_ast_pattern_object_t pattern = (neo_ast_pattern_object_t)node;
    for (neo_list_node_t it = neo_list_get_first(pattern->items);
         it != neo_list_get_tail(pattern->items); it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_PATTERN_ARRAY: {
    neo_ast_pattern_array_t pattern = (neo_ast_pattern_array_t)node;
    for (neo_list_node_t it = neo_list_get_first(pattern->items);
         it != neo_list_get_tail(pattern->items); it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_PATTERN_OBJECT_ITEM: {
    neo_ast_pattern_object_item_t item = (neo_ast_pattern_object_item_t)node;
    neo_resolve_closure(allocator, item->value, closure);
    if (item->identifier->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, item->identifier, closure);
    }
    if (item->alias && item->alias->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, item->alias, closure);
    }
  } break;
  case NEO_NODE_TYPE_PATTERN_ARRAY_ITEM: {
    neo_ast_pattern_array_item_t item = (neo_ast_pattern_array_item_t)node;
    neo_resolve_closure(allocator, item->value, closure);
    if (item->identifier->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, item->identifier, closure);
    }
  } break;
  case NEO_NODE_TYPE_PATTERN_REST: {
    neo_ast_pattern_rest_t item = (neo_ast_pattern_rest_t)node;
    if (item->identifier->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, item->identifier, closure);
    }
  } break;
  case NEO_NODE_TYPE_CLASS_ACCESSOR: {
    neo_ast_class_accessor_t function = (neo_ast_class_accessor_t)node;
    if (function->computed) {
      neo_resolve_closure(allocator, function->name, closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->decorators);
         it != neo_list_get_tail(function->decorators);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->arguments);
         it != neo_list_get_tail(function->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->closure);
         it != neo_list_get_tail(function->closure);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_CLASS_METHOD: {
    neo_ast_class_method_t function = (neo_ast_class_method_t)node;
    if (function->computed) {
      neo_resolve_closure(allocator, function->name, closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->decorators);
         it != neo_list_get_tail(function->decorators);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->arguments);
         it != neo_list_get_tail(function->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->closure);
         it != neo_list_get_tail(function->closure);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_CLASS_PROPERTY: {
    neo_ast_class_property_t protperty = (neo_ast_class_property_t)node;

    neo_resolve_closure(allocator, protperty->value, closure);
    if (protperty->computed) {
      neo_resolve_closure(allocator, protperty->identifier, closure);
    }
    for (neo_list_node_t it = neo_list_get_first(protperty->decorators);
         it != neo_list_get_tail(protperty->decorators);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_STATIC_BLOCK: {
    neo_ast_static_block_t staticblock = (neo_ast_static_block_t)node;
    for (neo_list_node_t it = neo_list_get_first(staticblock->body);
         it != neo_list_get_tail(staticblock->body);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_IMPORT_SPECIFIER:
    break;
  case NEO_NODE_TYPE_IMPORT_DEFAULT:
    break;
  case NEO_NODE_TYPE_IMPORT_NAMESPACE:
    break;
  case NEO_NODE_TYPE_IMPORT_ATTRIBUTE:
    break;
  case NEO_NODE_TYPE_EXPORT_SPECIFIER: {
    neo_ast_export_specifier_t specifier = (neo_ast_export_specifier_t)node;
    neo_resolve_closure(allocator, specifier->identifier, closure);
  } break;
  case NEO_NODE_TYPE_EXPORT_DEFAULT: {
    neo_ast_export_default_t specifier = (neo_ast_export_default_t)node;
    neo_resolve_closure(allocator, specifier->value, closure);
  } break;
  case NEO_NODE_TYPE_EXPORT_NAMESPACE:
    break;
  case NEO_NODE_TYPE_FUNCTION_BODY: {
    neo_ast_function_body_t body = (neo_ast_function_body_t)node;
    for (neo_list_node_t it = neo_list_get_first(body->body);
         it != neo_list_get_tail(body->body); it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_FUNCTION_ARGUMENT: {
    neo_ast_function_argument_t argument = (neo_ast_function_argument_t)node;
    neo_resolve_closure(allocator, argument->value, closure);
    if (argument->identifier->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, argument->identifier, closure);
    }
  } break;
  case NEO_NODE_TYPE_OBJECT_PROPERTY: {
    neo_ast_object_property_t property = (neo_ast_object_property_t)node;
    neo_resolve_closure(allocator, property->value, closure);
    if (property->identifier->type != NEO_NODE_TYPE_IDENTIFIER) {
      neo_resolve_closure(allocator, property->identifier, closure);
    }
  } break;
  case NEO_NODE_TYPE_OBJECT_METHOD: {
    neo_ast_object_method_t function = (neo_ast_object_method_t)node;
    if (function->computed) {
      neo_resolve_closure(allocator, function->name, closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->arguments);
         it != neo_list_get_tail(function->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->closure);
         it != neo_list_get_tail(function->closure);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  case NEO_NODE_TYPE_OBJECT_ACCESSOR: {
    neo_ast_object_accessor_t function = (neo_ast_object_accessor_t)node;
    if (function->computed) {
      neo_resolve_closure(allocator, function->name, closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->arguments);
         it != neo_list_get_tail(function->arguments);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
    for (neo_list_node_t it = neo_list_get_first(function->closure);
         it != neo_list_get_tail(function->closure);
         it = neo_list_node_next(it)) {
      neo_resolve_closure(allocator, neo_list_node_get(it), closure);
    }
  } break;
  }
}