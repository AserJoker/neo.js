#include "compiler/asm.h"
#include "compiler/ast_identifier.h"
#include "compiler/ast_node.h"
#include "compiler/ast_pattern_array.h"
#include "compiler/ast_pattern_object.h"
#include "compiler/ast_statement_block.h"
#include "compiler/ast_try_catch.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/location.h"

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
static neo_any_t neo_serialize_ast_try_catch(neo_allocator_t allocator,
                                             neo_ast_try_catch_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_SWITCH_CASE"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "error",
              neo_ast_node_serialize(allocator, node->error));
  neo_any_set(variable, "body", neo_ast_node_serialize(allocator, node->body));
  return variable;
}
static void neo_ast_try_catch_write(neo_allocator_t allocator,
                                    neo_write_context_t ctx,
                                    neo_ast_try_catch_t self) {
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  if (self->error) {
    if (self->error->type == NEO_NODE_TYPE_IDENTIFIER) {
      char *name = neo_location_get(allocator, self->error->location);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
      neo_js_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    } else {
      self->error->write(allocator, ctx, self->error);
    }
  } else {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  }
  self->body->write(allocator, ctx, self->body);
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
}

static neo_ast_try_catch_t neo_create_ast_try_catch(neo_allocator_t allocator) {
  neo_ast_try_catch_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_try_catch_t),
                          neo_ast_try_catch_dispose);
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
                                      const char *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_try_catch_t node = neo_create_ast_try_catch(allocator);
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  neo_ast_node_t error = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "catch")) {
    goto onerror;
  }
  scope =
      neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK, false, false);
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset == '(') {
    current.column++;
    current.offset++;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->error = neo_ast_read_pattern_object(allocator, file, &current);
    if (!node->error) {
      node->error = neo_ast_read_pattern_array(allocator, file, &current);
    }
    if (!node->error) {
      node->error = neo_ast_read_identifier(allocator, file, &current);
    }
    if (!node->error) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    NEO_CHECK_NODE(node->error, error, onerror);
    error = neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                     node->error, NEO_COMPILE_VARIABLE_LET);
    if (error) {
      goto onerror;
    }
    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    if (*current.offset != ')') {
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
  }
  node->body = neo_ast_read_statement_block(allocator, file, &current);
  if (!node->body) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->body, error, onerror);
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
  return error;
}