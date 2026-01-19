
#include "neojs/compiler/ast_program.h"
#include "neojs/compiler/asm.h"
#include "neojs/compiler/ast_directive.h"
#include "neojs/compiler/ast_interpreter.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_statement.h"
#include "neojs/compiler/program.h"
#include "neojs/compiler/scope.h"
#include "neojs/core/allocator.h"
#include "neojs/core/any.h"
#include "neojs/core/list.h"
#include "neojs/core/location.h"

static void neo_ast_program_dispose(neo_allocator_t allocator,
                                    neo_ast_program_t node) {
  neo_allocator_free(allocator, node->interpreter);
  neo_allocator_free(allocator, node->directives);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_program_resolve_closure(neo_allocator_t allocator,
                                            neo_ast_program_t self,
                                            neo_list_t closure) {
  for (neo_list_node_t it = neo_list_get_first(self->body);
       it != neo_list_get_tail(self->body); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static void neo_ast_program_write(neo_allocator_t allocator,
                                  neo_write_context_t ctx,
                                  neo_ast_program_t self) {
  neo_ast_node_t error = NULL;
  bool is_async = ctx->is_async;
  ctx->is_async = true;
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
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_HLT);
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  ctx->is_async = is_async;
}

static neo_any_t neo_serialize_ast_program(neo_allocator_t allocator,
                                           neo_ast_program_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_PROGRAM"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "interpreter",
              neo_ast_node_serialize(allocator, node->interpreter));
  neo_any_set(variable, "directives",
              neo_ast_node_list_serialize(allocator, node->directives));
  neo_any_set(variable, "body",
              neo_ast_node_list_serialize(allocator, node->body));
  return variable;
}

static neo_ast_program_t neo_create_ast_program(neo_allocator_t allocator) {
  neo_ast_program_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_program_t), neo_ast_program_dispose);
  node->node.type = NEO_NODE_TYPE_PROGRAM;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_program;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_program_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_program_write;
  node->interpreter = NULL;
  neo_list_initialize_t initialize = {0};
  initialize.auto_free = true;
  node->directives = neo_create_list(allocator, &initialize);
  node->body = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_program(neo_allocator_t allocator, const char *file,
                                    neo_position_t *position) {
  neo_position_t current = *position;
  neo_compile_scope_t scope = neo_compile_scope_push(
      allocator, NEO_COMPILE_SCOPE_FUNCTION, false, true);
  neo_ast_program_t node = neo_create_ast_program(allocator);
  neo_ast_node_t error = NULL;
  node->interpreter = neo_ast_read_interpreter(allocator, file, &current);
  if (node->interpreter && node->interpreter->type == NEO_NODE_TYPE_ERROR) {
    error = node->interpreter;
    node->interpreter = NULL;
    goto onerror;
  }
  error = neo_skip_all(allocator, file, &current);
  if (error && error->type == NEO_NODE_TYPE_ERROR) {
    goto onerror;
  }
  for (;;) {
    neo_ast_node_t directive =
        neo_ast_read_directive(allocator, file, &current);
    if (!directive) {
      break;
    }
    if (directive->type == NEO_NODE_TYPE_ERROR) {
      error = directive;
      goto onerror;
    };
    neo_list_push(node->directives, directive);
    error = neo_skip_all(allocator, file, &current);
    if (error && error->type == NEO_NODE_TYPE_ERROR) {
      goto onerror;
    }
    if (*current.offset == ';') {
      current.offset++;
      current.column++;
      error = neo_skip_all(allocator, file, &current);
      if (error && error->type == NEO_NODE_TYPE_ERROR) {
        goto onerror;
      }
    }
  }
  error = neo_skip_all(allocator, file, &current);
  if (error && error->type == NEO_NODE_TYPE_ERROR) {
    goto onerror;
  }
  for (;;) {
    neo_ast_node_t statement =
        neo_ast_read_statement(allocator, file, &current);
    if (!statement) {
      break;
    }
    if (statement->type == NEO_NODE_TYPE_ERROR) {
      error = statement;
      goto onerror;
    }
    neo_list_push(node->body, statement);
    error = neo_skip_all(allocator, file, &current);
    if (error && error->type == NEO_NODE_TYPE_ERROR) {
      goto onerror;
    }
    if (*current.offset == ';') {
      current.offset++;
      current.column++;
      error = neo_skip_all(allocator, file, &current);
      if (error && error->type == NEO_NODE_TYPE_ERROR) {
        goto onerror;
      }
    }
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  node->node.scope = neo_compile_scope_pop(scope);
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  scope = neo_compile_scope_pop(scope);
  neo_allocator_free(allocator, scope);
  return error;
}