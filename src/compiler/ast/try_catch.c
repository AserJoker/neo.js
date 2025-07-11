#include "compiler/ast/try_catch.h"
#include "compiler/asm.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/ast/statement_block.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_try_catch_dispose(neo_allocator_t allocator,
                                      neo_ast_try_catch_t node) {
  neo_allocator_free(allocator, node->error);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_try_catch_resolve_closure(neo_allocator_t allocator,
                                              neo_ast_try_catch_t self,
                                              neo_list_t closure) {
  neo_compile_scope_t scope = neo_compile_scope_set(self->node.scope);
  if (self->error && self->error->type != NEO_NODE_TYPE_IDENTIFIER) {
    self->error->resolve_closure(allocator, self->error, closure);
  }
  self->body->resolve_closure(allocator, self->body, closure);
  neo_compile_scope_set(scope);
}
static neo_variable_t neo_serialize_ast_try_catch(neo_allocator_t allocator,
                                                  neo_ast_try_catch_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_SWITCH_CASE"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"error",
                   neo_ast_node_serialize(allocator, node->error));
  neo_variable_set(variable, L"body",
                   neo_ast_node_serialize(allocator, node->body));
  return variable;
}
static void neo_ast_try_catch_write(neo_allocator_t allocator,
                                    neo_write_context_t ctx,
                                    neo_ast_try_catch_t self) {
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  if (self->error) {
    if (self->error->type == NEO_NODE_TYPE_IDENTIFIER) {
      wchar_t *name = neo_location_get(allocator, self->error->location);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
      neo_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
    } else {
      TRY(self->error->write(allocator, ctx, self->error)) { return; }
    }
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  }
  TRY(self->body->write(allocator, ctx, self->body)) { return; }
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
}

static neo_ast_try_catch_t neo_create_ast_try_catch(neo_allocator_t allocator) {
  neo_ast_try_catch_t node = neo_allocator_alloc2(allocator, neo_ast_try_catch);
  node->node.type = NEO_NODE_TYPE_TRY_CATCH;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_try_catch;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_try_catch_resolve_closure;
  node->body = NULL;
  node->error = NULL;
  node->node.write = (neo_write_fn_t)neo_ast_try_catch_write;
  return node;
}

neo_ast_node_t neo_ast_read_try_catch(neo_allocator_t allocator,
                                      const wchar_t *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_try_catch_t node = neo_create_ast_try_catch(allocator);
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "catch")) {
    goto onerror;
  }
  scope =
      neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK, false, false);
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset == '(') {
    current.column++;
    current.offset++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->error = TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
      goto onerror;
    };
    if (!node->error) {
      node->error = TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
        goto onerror;
      }
    }
    if (!node->error) {
      node->error = TRY(neo_ast_read_identifier(allocator, file, &current)) {
        goto onerror;
      }
    }
    if (!node->error) {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
    TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                 node->error, NEO_COMPILE_VARIABLE_LET)) {
      goto onerror;
    };
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset != ')') {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
  }
  node->body = TRY(neo_ast_read_statement_block(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->body) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  node->node.scope = neo_compile_scope_pop(scope);
  *position = current;
  return &node->node;
onerror:
  if (scope && !node->node.scope) {
    scope = neo_compile_scope_pop(scope);
    neo_allocator_free(allocator, scope);
  }
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}