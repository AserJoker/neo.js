#include "neojs/compiler/ast_expression_assigment.h"
#include "neojs/compiler/asm.h"
#include "neojs/compiler/ast_expression.h"
#include "neojs/compiler/ast_expression_member.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_pattern_array.h"
#include "neojs/compiler/ast_pattern_object.h"
#include "neojs/compiler/program.h"
#include "neojs/compiler/token.h"
#include "neojs/compiler/writer.h"
#include "neojs/core/allocator.h"
#include "neojs/core/any.h"
#include "neojs/core/list.h"
#include "neojs/core/location.h"
#include "neojs/core/position.h"
#include <stdbool.h>
#include <stdio.h>

static void
neo_ast_expression_assigment_dispose(neo_allocator_t allocator,
                                     neo_ast_expression_assigment_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->opt);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_expression_assigment_resolve_closure(
    neo_allocator_t allocator, neo_ast_expression_assigment_t self,
    neo_list_t closure) {
  self->identifier->resolve_closure(allocator, self->identifier, closure);
  self->value->resolve_closure(allocator, self->value, closure);
}

static void
neo_ast_expression_assigment_write(neo_allocator_t allocator,
                                   neo_write_context_t ctx,
                                   neo_ast_expression_assigment_t self) {
  neo_ast_node_t error = NULL;
  if (self->identifier->type == NEO_NODE_TYPE_EXPRESSION_MEMBER ||
      self->identifier->type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
    neo_ast_expression_member_t member =
        (neo_ast_expression_member_t)self->identifier;
    if (member->host->type != NEO_NODE_TYPE_EXPRESSION_SUPER) {
      neo_list_initialize_t initialize = {true};
      neo_list_t addresses = neo_create_list(allocator, &initialize);
      neo_write_optional_chain(allocator, ctx, member->host, addresses);
      neo_allocator_free(allocator, addresses);
    }
    if (self->identifier->type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
      if (member->field->type != NEO_NODE_TYPE_PRIVATE_NAME) {
        char *field = neo_location_get(allocator, member->field->location);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_js_program_add_string(allocator, ctx->program, field);
        neo_allocator_free(allocator, field);
      }
    } else {
      member->field->write(allocator, ctx, member->field);
    }
    if (!neo_location_is(self->opt->location, "=")) {
      self->identifier->write(allocator, ctx, self->identifier);
      self->value->write(allocator, ctx, self->value);
      if (neo_location_is(self->opt->location, "+=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_ADD);
      } else if (neo_location_is(self->opt->location, "-=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SUB);
      } else if (neo_location_is(self->opt->location, "*=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_MUL);
      } else if (neo_location_is(self->opt->location, "/=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DIV);
      } else if (neo_location_is(self->opt->location, "%=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_MOD);
      } else if (neo_location_is(self->opt->location, "**=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POW);
      } else if (neo_location_is(self->opt->location, "&=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_AND);
      } else if (neo_location_is(self->opt->location, "|=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_OR);
      } else if (neo_location_is(self->opt->location, "^=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_XOR);
      } else if (neo_location_is(self->opt->location, "~=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NOT);
      } else if (neo_location_is(self->opt->location, ">>=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SHR);
      } else if (neo_location_is(self->opt->location, "<<=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SHL);
      } else if (neo_location_is(self->opt->location, ">>>=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_USHR);
      }
    } else {
      self->value->write(allocator, ctx, self->value);
    }
    if (member->host->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SET_SUPER_FIELD);
    } else {
      if (member->field->type == NEO_NODE_TYPE_PRIVATE_NAME) {
        neo_js_program_add_code(allocator, ctx->program,
                                NEO_ASM_SET_PRIVATE_FIELD);
        char *name = neo_location_get(allocator, member->field->location);
        neo_js_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
      }
    }
  } else if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    if (!neo_location_is(self->opt->location, "=")) {
      self->identifier->write(allocator, ctx, self->identifier);
      self->value->write(allocator, ctx, self->value);
      if (neo_location_is(self->opt->location, "+=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_ADD);
      } else if (neo_location_is(self->opt->location, "-=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SUB);
      } else if (neo_location_is(self->opt->location, "*=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_MUL);
      } else if (neo_location_is(self->opt->location, "/=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DIV);
      } else if (neo_location_is(self->opt->location, "%=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_MOD);
      } else if (neo_location_is(self->opt->location, "**=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POW);
      } else if (neo_location_is(self->opt->location, "&=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_AND);
      } else if (neo_location_is(self->opt->location, "|=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_OR);
      } else if (neo_location_is(self->opt->location, "^=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_XOR);
      } else if (neo_location_is(self->opt->location, "~=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NOT);
      } else if (neo_location_is(self->opt->location, ">>=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SHR);
      } else if (neo_location_is(self->opt->location, "<<=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SHL);
      } else if (neo_location_is(self->opt->location, ">>>=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_USHR);
      }
    } else {
      self->value->write(allocator, ctx, self->value);
    }
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_js_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  }
}

static neo_any_t
neo_serialize_ast_expression_assigment(neo_allocator_t allocator,
                                       neo_ast_expression_assigment_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPRESSION_ASSIGMENT"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  neo_any_set(variable, "value",
              neo_ast_node_serialize(allocator, node->value));
  return variable;
}

static neo_ast_expression_assigment_t
neo_create_ast_expression_assigment(neo_allocator_t allocator) {
  neo_ast_expression_assigment_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_expression_assigment_t),
      neo_ast_expression_assigment_dispose);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_ASSIGMENT;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_assigment;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_assigment_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_expression_assigment_write;
  node->identifier = NULL;
  node->value = NULL;
  node->opt = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_expression_assigment(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position) {
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_ast_expression_assigment_t node = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_expression_assigment(allocator);
  node->identifier = neo_ast_read_expression_17(allocator, file, &current);
  if (!node->identifier) {
    node->identifier = neo_ast_read_pattern_object(allocator, file, &current);
  }
  if (!node->identifier) {
    node->identifier = neo_ast_read_pattern_array(allocator, file, &current);
  }
  if (!node->identifier) {
    goto onerror;
  }
  NEO_CHECK_NODE(node->identifier, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  token = neo_read_symbol_token(allocator, file, &current);
  if (!token) {
    goto onerror;
  }
  if (!neo_location_is(token->location, "=") &&
      !neo_location_is(token->location, "+=") &&
      !neo_location_is(token->location, "-=") &&
      !neo_location_is(token->location, "**=") &&
      !neo_location_is(token->location, "*=") &&
      !neo_location_is(token->location, "/=") &&
      !neo_location_is(token->location, "%=") &&
      !neo_location_is(token->location, "<<=") &&
      !neo_location_is(token->location, ">>=") &&
      !neo_location_is(token->location, ">>>=") &&
      !neo_location_is(token->location, "&=") &&
      !neo_location_is(token->location, "|=") &&
      !neo_location_is(token->location, "^=") &&
      !neo_location_is(token->location, "&&=") &&
      !neo_location_is(token->location, "||=") &&
      !neo_location_is(token->location, "(?\?=)")) {
    neo_allocator_free(allocator, token);
    goto onerror;
  }
  node->opt = token;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->value = neo_ast_read_expression_2(allocator, file, &current);
  if (!node->value) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->value, error, onerror);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}