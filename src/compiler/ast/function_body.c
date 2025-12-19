#include "compiler/ast/function_body.h"
#include "compiler/asm.h"
#include "compiler/ast/directive.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/list.h"
#include "core/position.h"

static void neo_ast_function_body_dispose(neo_allocator_t allocator,
                                          neo_ast_function_body_t node) {
  neo_allocator_free(allocator, node->directives);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_function_body_resolve_closure(neo_allocator_t allocator,
                                                  neo_ast_function_body_t self,
                                                  neo_list_t closure) {
  neo_compile_scope_t scope = neo_compile_scope_set(self->node.scope);
  for (neo_list_node_t it = neo_list_get_first(self->body);
       it != neo_list_get_tail(self->body); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
  neo_compile_scope_set(scope);
}
static void neo_ast_function_body_write(neo_allocator_t allocator,
                                        neo_write_context_t ctx,
                                        neo_ast_function_body_t self) {
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  for (neo_list_node_t it = neo_list_get_first(self->directives);
       it != neo_list_get_tail(self->directives); it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    item->write(allocator, ctx, item);
  }
  for (neo_list_node_t it = neo_list_get_first(self->body);
       it != neo_list_get_tail(self->body); it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    item->write(allocator, ctx, item);
  }
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_RET);
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
}
static neo_any_t neo_serialize_ast_function_body(neo_allocator_t allocator,
                                                 neo_ast_function_body_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_FUNCTION_BODY"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "body",
              neo_ast_node_list_serialize(allocator, node->body));
  neo_any_set(variable, "directives",
              neo_ast_node_list_serialize(allocator, node->directives));
  return variable;
}

static neo_ast_function_body_t
neo_create_ast_function_body(neo_allocator_t allocator) {
  neo_ast_function_body_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_function_body_t),
                          neo_ast_function_body_dispose);
  neo_list_initialize_t initialize = {true};
  node->directives = neo_create_list(allocator, &initialize);
  node->body = neo_create_list(allocator, &initialize);
  node->node.type = NEO_NODE_TYPE_FUNCTION_BODY;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_function_body;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_function_body_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_function_body_write;
  return node;
}

neo_ast_node_t neo_ast_read_function_body(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_position_t current = *position;
  neo_compile_scope_t scope = NULL;
  neo_ast_function_body_t node = NULL;
  neo_ast_node_t error = NULL;
  if (*current.offset != '{') {
    return NULL;
  }
  current.offset++;
  current.column++;
  node = neo_create_ast_function_body(allocator);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  scope =
      neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK, false, false);
  for (;;) {
    neo_ast_node_t directive =
        neo_ast_read_directive(allocator, file, &current);
    if (!directive) {
      break;
    }
    NEO_CHECK_NODE(directive, error, onerror);
    neo_list_push(node->directives, directive);

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    if (*current.offset == ';') {
      current.offset++;
      current.column++;

      error = neo_skip_all(allocator, file, &current);
      if (error) {
        goto onerror;
      }
    }
  }

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  for (;;) {
    neo_ast_node_t statement =
        neo_ast_read_statement(allocator, file, &current);
    if (!statement) {
      break;
    }
    NEO_CHECK_NODE(statement, error, onerror);
    neo_list_push(node->body, statement);

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    if (*current.offset == ';') {
      current.offset++;
      current.column++;

      error = neo_skip_all(allocator, file, &current);
      if (error) {
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
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  } else {
    current.offset++;
    current.column++;
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
  neo_allocator_free(allocator, node);
  return error;
}