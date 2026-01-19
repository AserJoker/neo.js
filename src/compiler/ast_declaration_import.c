#include "neojs/compiler/ast_declaration_import.h"
#include "neojs/compiler/asm.h"
#include "neojs/compiler/ast_import_attribute.h"
#include "neojs/compiler/ast_import_default.h"
#include "neojs/compiler/ast_import_namespace.h"
#include "neojs/compiler/ast_import_specifier.h"
#include "neojs/compiler/ast_literal_string.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/program.h"
#include "neojs/compiler/token.h"
#include "neojs/core/allocator.h"
#include "neojs/core/any.h"
#include "neojs/core/list.h"
#include "neojs/core/location.h"
#include <stdio.h>
#include <string.h>

static void
neo_ast_declaration_import_dispose(neo_allocator_t allocator,
                                   neo_ast_declaration_import_t node) {
  neo_allocator_free(allocator, node->specifiers);
  neo_allocator_free(allocator, node->attributes);
  neo_allocator_free(allocator, node->source);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_declaration_import_write(neo_allocator_t allocator,
                                 neo_write_context_t ctx,
                                 neo_ast_declaration_import_t self) {
  char *name = neo_location_get(allocator, self->source->location);
  name[strlen(name) - 1] = 0;
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_IMPORT);
  neo_js_program_add_string(allocator, ctx->program, name + 1);
  for (neo_list_node_t it = neo_list_get_first(self->attributes);
       it != neo_list_get_tail(self->attributes); it = neo_list_node_next(it)) {
    neo_ast_node_t attr = neo_list_node_get(it);
    attr->write(allocator, ctx, attr);
    neo_js_program_add_string(allocator, ctx->program, name + 1);
  }
  neo_allocator_free(allocator, name);
  for (neo_list_node_t it = neo_list_get_first(self->specifiers);
       it != neo_list_get_tail(self->specifiers); it = neo_list_node_next(it)) {
    neo_ast_node_t spec = neo_list_node_get(it);
    spec->write(allocator, ctx, spec);
  }
}

static neo_any_t
neo_serialize_ast_declaration_import(neo_allocator_t allocator,
                                     neo_ast_declaration_import_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_DECLARATION_IMPORT"));
  neo_any_set(variable, "specifiers",
              neo_ast_node_list_serialize(allocator, node->specifiers));
  neo_any_set(variable, "attributes",
              neo_ast_node_list_serialize(allocator, node->attributes));
  neo_any_set(variable, "source",
              neo_ast_node_serialize(allocator, node->source));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_declaration_import_t
neo_create_ast_declaration_import(neo_allocator_t allocator) {
  neo_ast_declaration_import_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_declaration_import_t),
      neo_ast_declaration_import_dispose);
  node->node.type = NEO_NODE_TYPE_DECLARATION_IMPORT;

  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_declaration_import;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_declaration_import_write;
  neo_list_initialize_t initialize = {true};
  node->specifiers = neo_create_list(allocator, &initialize);
  node->attributes = neo_create_list(allocator, &initialize);
  node->source = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_declaration_import(neo_allocator_t allocator,
                                               const char *file,
                                               neo_position_t *position) {
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_ast_declaration_import_t node =
      neo_create_ast_declaration_import(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "import")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->source = neo_ast_read_literal_string(allocator, file, &current);
  if (!node->source) {
    if (*current.offset == '*') {
      neo_ast_node_t specifier =
          neo_ast_read_import_namespace(allocator, file, &current);
      if (!specifier) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
        goto onerror;
      }
      NEO_CHECK_NODE(specifier, error, onerror);
      neo_list_push(node->specifiers, specifier);
    } else {
      neo_ast_node_t specifier =
          neo_ast_read_import_default(allocator, file, &current);
      if (specifier) {
        NEO_CHECK_NODE(specifier, error, onerror);
        neo_list_push(node->specifiers, specifier);
        error = neo_skip_all(allocator, file, &current);
        if (error) {
          goto onerror;
        }
        if (*current.offset == ',') {
          current.offset++;
          current.column++;
          error = neo_skip_all(allocator, file, &current);
          if (error) {
            goto onerror;
          }
        } else {
          goto from;
        }
      }
      if (*current.offset == '{') {
        current.offset++;
        current.column++;
        error = neo_skip_all(allocator, file, &current);
        if (error) {
          goto onerror;
        }
        if (*current.offset != '}') {
          for (;;) {
            specifier =
                neo_ast_read_import_specifier(allocator, file, &current);
            if (!specifier) {
              break;
            }
            NEO_CHECK_NODE(specifier, error, onerror);
            neo_list_push(node->specifiers, specifier);
            error = neo_skip_all(allocator, file, &current);
            if (error) {
              goto onerror;
            }
            if (*current.offset == '}') {
              break;
            } else if (*current.offset == ',') {
              current.offset++;
              current.column++;
              error = neo_skip_all(allocator, file, &current);
              if (error) {
                goto onerror;
              }
            } else {
              error = neo_create_error_node(
                  allocator,
                  "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
                  file, current.line, current.column);
              goto onerror;
            }
          }
        }
        error = neo_skip_all(allocator, file, &current);
        if (error) {
          goto onerror;
        }
        if (*current.offset != '}') {
          error = neo_create_error_node(
              allocator,
              "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
              current.line, current.column);
          goto onerror;
        }
        current.column++;
        current.offset++;
      }
    }
  from:

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    token = neo_read_identify_token(allocator, file, &current);
    if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
      error = neo_create_error_node(allocator, NULL);
      error->error = token->error;
      token->error = NULL;
      goto onerror;
    }
    if (!token || !neo_location_is(token->location, "from")) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    neo_allocator_free(allocator, token);

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->source = neo_ast_read_literal_string(allocator, file, &current);
    if (!node->source) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    NEO_CHECK_NODE(node->source, error, onerror);
  }
  NEO_CHECK_NODE(node->source, error, onerror);
  neo_position_t curr = current;
  error = neo_skip_all(allocator, file, &curr);
  if (error) {
    goto onerror;
  }
  token = neo_read_identify_token(allocator, file, &curr);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (token && neo_location_is(token->location, "assert")) {
    current = curr;
    neo_allocator_free(allocator, token);

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    if (*current.offset != '{') {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    current.offset++;
    current.column++;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    if (*current.offset != '}') {
      for (;;) {
        neo_ast_node_t attribute =
            neo_ast_read_import_attribute(allocator, file, &current);
        if (!attribute) {
          error = neo_create_error_node(
              allocator,
              "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
              current.line, current.column);
          goto onerror;
        }
        NEO_CHECK_NODE(attribute, error, onerror);
        neo_list_push(node->attributes, attribute);
        error = neo_skip_all(allocator, file, &current);
        if (error) {
          goto onerror;
        }
        if (*current.offset == '}') {
          break;
        } else if (*current.offset == ',') {
          current.offset++;
          current.column++;
          error = neo_skip_all(allocator, file, &current);
          if (error) {
            goto onerror;
          }
        } else {
          error = neo_create_error_node(
              allocator,
              "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
              current.line, current.column);
          goto onerror;
        }
      }
      if (*current.offset != '}') {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
        goto onerror;
      }
      current.offset++;
      current.column++;
    }
  }
  neo_allocator_free(allocator, token);
  uint32_t line = current.line;
  neo_position_t cur = current;
  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  if (cur.line == line) {
    if (*cur.offset && *cur.offset != ';') {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, cur.line, cur.column);
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
  return error;
}