#include "compiler/declaration_import.h"
#include "compiler/import_attribute.h"
#include "compiler/import_default.h"
#include "compiler/import_namespace.h"
#include "compiler/import_specifier.h"
#include "compiler/literal_string.h"
#include "compiler/token.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_declaration_import_dispose(neo_allocator_t allocator,
                                   neo_ast_declaration_import_t node) {
  neo_allocator_free(allocator, node->specifiers);
  neo_allocator_free(allocator, node->attributes);
  neo_allocator_free(allocator, node->source);
}

static neo_variable_t
neo_serialize_ast_declaration_import(neo_allocator_t allocator,
                                     neo_ast_declaration_import_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_DECLARATION_IMPORT"));
  neo_variable_set(variable, "specifiers",
                   neo_ast_node_list_serialize(allocator, node->specifiers));
  neo_variable_set(variable, "attributes",
                   neo_ast_node_list_serialize(allocator, node->attributes));
  neo_variable_set(variable, "source",
                   neo_ast_node_serialize(allocator, node->source));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}

static neo_ast_declaration_import_t
neo_create_ast_declaration_import(neo_allocator_t allocator) {
  neo_ast_declaration_import_t node =
      neo_allocator_alloc2(allocator, neo_ast_declaration_import);
  node->node.type = NEO_NODE_TYPE_DECLARATION_IMPORT;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_declaration_import;
  neo_list_initialize_t initialize = {true};
  node->specifiers = neo_create_list(allocator, &initialize);
  node->attributes = neo_create_list(allocator, &initialize);
  node->source = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_declaration_import(neo_allocator_t allocator,
                                               const char *file,
                                               neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_declaration_import_t node =
      neo_create_ast_declaration_import(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "import")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->source = TRY(neo_ast_read_literal_string(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->source) {
    if (*current.offset == '*') {
      neo_ast_node_t specifier =
          TRY(neo_ast_read_import_namespace(allocator, file, &current)) {
        goto onerror;
      }
      if (!specifier) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->specifiers, specifier);
    } else {
      neo_ast_node_t specifier =
          TRY(neo_ast_read_import_default(allocator, file, &current)) {
        goto onerror;
      }
      if (specifier) {
        neo_list_push(node->specifiers, specifier);
        SKIP_ALL(allocator, file, &current, onerror);
        if (*current.offset == ',') {
          current.offset++;
          current.column++;
          SKIP_ALL(allocator, file, &current, onerror);
        } else {
          goto from;
        }
      }
      if (*current.offset == '{') {
        current.offset++;
        current.column++;
        SKIP_ALL(allocator, file, &current, onerror);
        if (*current.offset != '}') {
          for (;;) {
            specifier =
                TRY(neo_ast_read_import_specifier(allocator, file, &current)) {
              goto onerror;
            }
            neo_list_push(node->specifiers, specifier);
            SKIP_ALL(allocator, file, &current, onerror);
            if (*current.offset == '}') {
              break;
            } else if (*current.offset == ',') {
              current.offset++;
              current.column++;
              SKIP_ALL(allocator, file, &current, onerror);
            } else {
              THROW("SyntaxError",
                    "Invalid or unexpected token \n  at %s:%d:%d", file,
                    current.line, current.column);
              goto onerror;
            }
          }
        }
        if (*current.offset != '}') {
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          goto onerror;
        }
        current.column++;
        current.offset++;
      }
    }
  from:
    SKIP_ALL(allocator, file, &current, onerror);
    token = neo_read_identify_token(allocator, file, &current);
    if (!token || !neo_location_is(token->location, "from")) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    neo_allocator_free(allocator, token);
    SKIP_ALL(allocator, file, &current, onerror);
    node->source = TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->source) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
  }
  neo_position_t curr = current;
  SKIP_ALL(allocator, file, &curr, onerror);
  token = neo_read_identify_token(allocator, file, &curr);
  if (token && neo_location_is(token->location, "assert")) {
    current = curr;
    neo_allocator_free(allocator, token);
    SKIP_ALL(allocator, file, &current, onerror);
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
  }
  neo_allocator_free(allocator, token);
  uint32_t line = current.line;
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (cur.line == line) {
    if (*cur.offset && *cur.offset != ';') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            cur.line, cur.column);
      goto onerror;
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