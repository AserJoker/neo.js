#include "compiler/ast/expression_assigment.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/expression_member.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
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
  if (self->identifier->type == NEO_NODE_TYPE_EXPRESSION_MEMBER ||
      self->identifier->type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
    neo_ast_expression_member_t member =
        (neo_ast_expression_member_t)self->identifier;
    neo_list_initialize_t initialize = {true};
    neo_list_t addresses = neo_create_list(allocator, &initialize);
    TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
      neo_allocator_free(allocator, addresses);
      return;
    }
    if (neo_list_get_size(addresses)) {
      THROW("Invalid left-hand side in assignment \n  at _.compile (%ls:%d:%d)",
            ctx->program->filename, self->identifier->location.begin.line,
            self->identifier->location.begin.column);
      neo_allocator_free(allocator, addresses);
      return;
    }
    neo_allocator_free(allocator, addresses);
    if (self->identifier->type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
      char *field = neo_location_get(allocator, member->field->location);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      neo_program_add_string(allocator, ctx->program, field);
      neo_allocator_free(allocator, field);
    } else {
      TRY(member->field->write(allocator, ctx, member->field)) { return; }
    }
    if (!neo_location_is(self->opt->location, "=")) {
      TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
      TRY(self->value->write(allocator, ctx, self->value)) { return; }
      if (neo_location_is(self->opt->location, "+=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_ADD);
      } else if (neo_location_is(self->opt->location, "-=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SUB);
      } else if (neo_location_is(self->opt->location, "*=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_MUL);
      } else if (neo_location_is(self->opt->location, "/=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_DIV);
      } else if (neo_location_is(self->opt->location, "%=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_MOD);
      } else if (neo_location_is(self->opt->location, "**=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_POW);
      } else if (neo_location_is(self->opt->location, "&=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_AND);
      } else if (neo_location_is(self->opt->location, "|=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_OR);
      } else if (neo_location_is(self->opt->location, "^=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_XOR);
      } else if (neo_location_is(self->opt->location, "~=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_NOT);
      } else if (neo_location_is(self->opt->location, ">>=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SHR);
      } else if (neo_location_is(self->opt->location, "<<=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SHL);
      } else if (neo_location_is(self->opt->location, ">>>=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_USHR);
      }
    } else {
      TRY(self->value->write(allocator, ctx, self->value)) { return; }
    }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  } else if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    if (!neo_location_is(self->opt->location, "=")) {
      TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
      TRY(self->value->write(allocator, ctx, self->value)) { return; }
      if (neo_location_is(self->opt->location, "+=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_ADD);
      } else if (neo_location_is(self->opt->location, "-=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SUB);
      } else if (neo_location_is(self->opt->location, "*=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_MUL);
      } else if (neo_location_is(self->opt->location, "/=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_DIV);
      } else if (neo_location_is(self->opt->location, "%=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_MOD);
      } else if (neo_location_is(self->opt->location, "**=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_POW);
      } else if (neo_location_is(self->opt->location, "&=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_AND);
      } else if (neo_location_is(self->opt->location, "|=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_OR);
      } else if (neo_location_is(self->opt->location, "^=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_XOR);
      } else if (neo_location_is(self->opt->location, "~=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_NOT);
      } else if (neo_location_is(self->opt->location, ">>=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SHR);
      } else if (neo_location_is(self->opt->location, "<<=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SHL);
      } else if (neo_location_is(self->opt->location, ">>>=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_USHR);
      }
    } else {
      TRY(self->value->write(allocator, ctx, self->value)) { return; }
    }
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else {
    THROW("Invalid left-hand side in assignment \n  at _.compile (%ls:%d:%d)",
          ctx->program->filename, self->identifier->location.begin.line,
          self->identifier->location.begin.column);
    return;
  }
}

static neo_variable_t
neo_serialize_ast_expression_assigment(neo_allocator_t allocator,
                                       neo_ast_expression_assigment_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_EXPRESSION_ASSIGMENT"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  return variable;
}

static neo_ast_expression_assigment_t
neo_create_ast_expression_assigment(neo_allocator_t allocator) {
  neo_ast_expression_assigment_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_assigment);
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
                                                 const wchar_t *file,
                                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_assigment_t node = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_expression_assigment(allocator);
  node->identifier =
      TRY(neo_ast_read_expression_17(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = TRY(neo_read_symbol_token(allocator, file, &current)) {
    goto onerror;
  };
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
  SKIP_ALL(allocator, file, &current, onerror);
  node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->value) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}