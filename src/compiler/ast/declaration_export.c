#include "compiler/ast/declaration_export.h"
#include "compiler/ast/declaration_class.h"
#include "compiler/ast/declaration_function.h"
#include "compiler/ast/declaration_variable.h"
#include "compiler/ast/export_all.h"
#include "compiler/ast/export_default.h"
#include "compiler/ast/export_namespace.h"
#include "compiler/ast/export_specifier.h"
#include "compiler/ast/import_attribute.h"
#include "compiler/ast/literal_string.h"
#include "compiler/token.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_declaration_export_dispose(neo_allocator_t allocator,
                                   neo_ast_declaration_export_t node) {
  neo_allocator_free(allocator, node->attributes);
  neo_allocator_free(allocator, node->source);
  neo_allocator_free(allocator, node->specifiers);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_declaration_export_resolve_closure(neo_allocator_t allocator,
                                           neo_ast_declaration_export_t self,
                                           neo_list_t closure) {
  if (!self->source) {
    for (neo_list_node_t it = neo_list_get_first(self->specifiers);
         it != neo_list_get_tail(self->specifiers);
         it = neo_list_node_next(it)) {
      neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
      item->resolve_closure(allocator, item, closure);
    }
  }
}

static neo_variable_t
neo_serialize_ast_declaration_export(neo_allocator_t allocator,
                                     neo_ast_declaration_export_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_DECLARATION_EXPORT"));
  neo_variable_set(variable, "source",
                   neo_ast_node_serialize(allocator, node->source));
  neo_variable_set(variable, "attributes",
                   neo_ast_node_list_serialize(allocator, node->attributes));
  neo_variable_set(variable, "specifiers",
                   neo_ast_node_list_serialize(allocator, node->specifiers));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_declaration_export_t
neo_create_ast_declaration_export(neo_allocator_t allocator) {
  neo_ast_declaration_export_t node =
      neo_allocator_alloc2(allocator, neo_ast_declaration_export);
  node->node.type = NEO_NODE_TYPE_DECLARATION_EXPORT;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_declaration_export;
  node->source = NULL;
  neo_list_initialize_t initialize = {true};
  node->attributes = neo_create_list(allocator, &initialize);
  node->specifiers = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_declaration_export(neo_allocator_t allocator,
                                               const char *file,
                                               neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_declaration_export_t node =
      neo_create_ast_declaration_export(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "export")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  neo_ast_node_t specifier =
      TRY(neo_ast_read_declaration_variable(allocator, file, &current)) {
    goto onerror;
  }
  if (!specifier) {
    specifier =
        TRY(neo_ast_read_declaration_function(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!specifier) {
    specifier = TRY(neo_ast_read_declaration_class(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!specifier) {
    specifier = TRY(neo_ast_read_export_default(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (specifier) {
    neo_list_push(node->specifiers, specifier);
    goto onfinish;
  }
  if (!specifier) {
    specifier = TRY(neo_ast_read_export_namespace(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!specifier) {
    specifier = TRY(neo_ast_read_export_all(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!specifier) {
    if (*current.offset != '{') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset != '}') {
      for (;;) {
        specifier =
            TRY(neo_ast_read_export_specifier(allocator, file, &current)) {
          goto onerror;
        }
        if (!specifier) {
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          goto onerror;
        }
        neo_list_push(node->specifiers, specifier);
        SKIP_ALL(allocator, file, &current, onerror);
        if (*current.offset == ',') {
          current.offset++;
          current.column++;
          SKIP_ALL(allocator, file, &current, onerror);
        } else if (*current.offset == '}') {
          break;
        } else {
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          goto onerror;
        }
      }
    }
    current.offset++;
    current.column++;
  } else {
    neo_list_push(node->specifiers, specifier);
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  token = neo_read_identify_token(allocator, file, &cur);
  if (!token || !neo_location_is(token->location, "from")) {
    if (specifier && (specifier->type == NEO_NODE_TYPE_EXPORT_NAMESPACE ||
                      specifier->type == NEO_NODE_TYPE_EXPORT_ALL)) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            cur.line, cur.column);
      goto onerror;
    }
    neo_allocator_free(allocator, token);
  } else {
    neo_allocator_free(allocator, token);
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
    node->source = TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    }
    cur = current;
    SKIP_ALL(allocator, file, &cur, onerror);
    token = neo_read_identify_token(allocator, file, &cur);
    if (token && neo_location_is(token->location, "assert")) {
      current = cur;
      neo_allocator_free(allocator, token);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset != '{') {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset != '}') {
        for (;;) {
          neo_ast_node_t attribute =
              TRY(neo_ast_read_import_attribute(allocator, file, &current)) {
            goto onerror;
          }
          if (!attribute) {
            THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                  file, current.line, current.column);
            goto onerror;
          }
          neo_list_push(node->attributes, attribute);
          SKIP_ALL(allocator, file, &current, onerror);
          if (*current.offset == '}') {
            break;
          } else if (*current.offset == ',') {
            current.offset++;
            current.column++;
            SKIP_ALL(allocator, file, &current, onerror);
          } else {
            THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                  file, current.line, current.column);
            goto onerror;
          }
        }
        if (*current.offset != '}') {
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          goto onerror;
        }
        current.offset++;
        current.column++;
      }
    } else {
      neo_allocator_free(allocator, token);
    }
  }
onfinish: {
  uint32_t line = current.line;
  cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (cur.line == line) {
    if (*cur.offset && *cur.offset != ';') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            cur.line, cur.column);
      goto onerror;
    }
  }
}
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}