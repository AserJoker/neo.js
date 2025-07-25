#include "compiler/writer.h"
#include "compiler/asm.h"
#include "compiler/ast/expression_call.h"
#include "compiler/ast/expression_function.h"
#include "compiler/ast/expression_member.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "core/location.h"
#include <stddef.h>
#include <string.h>
#include <wchar.h>

struct _neo_write_scope_t {
  neo_map_t functions;
  neo_write_scope_t parent;
};

static void neo_write_scope_dispose(neo_allocator_t allocator,
                                    neo_write_scope_t scope) {
  neo_allocator_free(allocator, scope->functions);
}

static neo_write_scope_t neo_create_write_scope(neo_allocator_t allocator,
                                                neo_write_scope_t parent) {
  neo_write_scope_t scope = neo_allocator_alloc(
      allocator, sizeof(struct _neo_write_scope_t), neo_write_scope_dispose);
  scope->parent = parent;
  neo_map_initialize_t initialze;
  initialze.auto_free_key = false;
  initialze.auto_free_value = true;
  initialze.compare = NULL;
  scope->functions = neo_create_map(allocator, &initialze);
  return scope;
}

void neo_writer_push_scope(neo_allocator_t allocator, neo_write_context_t ctx,
                           neo_compile_scope_t scope) {
  if (scope) {
    ctx->scope = neo_create_write_scope(allocator, ctx->scope);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_SCOPE);
    for (neo_list_node_t it = neo_list_get_first(scope->variables);
         it != neo_list_get_tail(scope->variables);
         it = neo_list_node_next(it)) {
      neo_compile_variable_t variable = neo_list_node_get(it);
      wchar_t *name = neo_location_get(allocator, variable->node->location);
      switch (variable->type) {
      case NEO_COMPILE_VARIABLE_VAR:
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
        break;
      case NEO_COMPILE_VARIABLE_LET:
        neo_program_add_code(allocator, ctx->program,
                             NEO_ASM_PUSH_UNINITIALIZED);
        break;
      case NEO_COMPILE_VARIABLE_CONST:
        neo_program_add_code(allocator, ctx->program,
                             NEO_ASM_PUSH_UNINITIALIZED);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_CONST);
        break;
      case NEO_COMPILE_VARIABLE_USING:
        neo_program_add_code(allocator, ctx->program,
                             NEO_ASM_PUSH_UNINITIALIZED);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_USING);
        break;
      case NEO_COMPILE_VARIABLE_AWAIT_USING:
        neo_program_add_code(allocator, ctx->program,
                             NEO_ASM_PUSH_UNINITIALIZED);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_AWAIT_USING);
        break;
      case NEO_COMPILE_VARIABLE_FUNCTION: {
        neo_ast_expression_function_t func =
            (neo_ast_expression_function_t)variable->node;
        if (func->generator) {
          if (func->async) {
            neo_program_add_code(allocator, ctx->program,
                                 NEO_ASM_PUSH_ASYNC_GENERATOR);
          } else {
            neo_program_add_code(allocator, ctx->program,
                                 NEO_ASM_PUSH_GENERATOR);
          }
        } else {
          if (func->async) {
            neo_program_add_code(allocator, ctx->program,
                                 NEO_ASM_PUSH_ASYNC_FUNCTION);
          } else {
            neo_program_add_code(allocator, ctx->program,
                                 NEO_ASM_PUSH_FUNCTION);
          }
        }
        neo_allocator_free(allocator, name);
        wchar_t *funcname = neo_location_get(allocator, func->name->location);
        name = neo_allocator_alloc(
            allocator, (wcslen(funcname) + 1) * sizeof(wchar_t), NULL);
        wcscpy(name, funcname);
        name[wcslen(funcname)] = 0;
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_NAME);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, funcname);
        wchar_t *source = neo_location_get(allocator, func->node.location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_SOURCE);
        neo_program_add_string(allocator, ctx->program, source);
        neo_allocator_free(allocator, source);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_ADDRESS);
        size_t *address = neo_allocator_alloc(allocator, sizeof(size_t), NULL);
        *address = neo_buffer_get_size(ctx->program->codes);
        neo_program_add_address(allocator, ctx->program, 0);
        neo_map_set(ctx->scope->functions, variable, address, NULL);
      } break;
      }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_DEF);
      neo_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    }
  }
}

void neo_writer_pop_scope(neo_allocator_t allocator, neo_write_context_t ctx,
                          neo_compile_scope_t scope) {
  if (scope) {
    for (neo_list_node_t it = neo_list_get_first(scope->variables);
         it != neo_list_get_tail(scope->variables);
         it = neo_list_node_next(it)) {
      neo_compile_variable_t variable = neo_list_node_get(it);
      if (variable->type == NEO_COMPILE_VARIABLE_FUNCTION) {
        neo_ast_expression_function_t function =
            (neo_ast_expression_function_t)variable->node;
        size_t address =
            *(size_t *)neo_map_get(ctx->scope->functions, variable, NULL);
        neo_program_set_current(ctx->program, address);
        neo_writer_push_scope(allocator, ctx, function->node.scope);
        if (neo_list_get_size(function->arguments)) {
          neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
          neo_program_add_string(allocator, ctx->program, L"arguments");
          neo_program_add_code(allocator, ctx->program, NEO_ASM_ITERATOR);
          for (neo_list_node_t it = neo_list_get_first(function->arguments);
               it != neo_list_get_tail(function->arguments);
               it = neo_list_node_next(it)) {
            neo_ast_node_t argument = neo_list_node_get(it);
            TRY(argument->write(allocator, ctx, argument)) { return; }
          }
          neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
          neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
        }
        bool is_async = ctx->is_async;
        ctx->is_async = function->async;
        bool is_generator = ctx->is_generator;
        ctx->is_generator = function->generator;
        TRY(function->body->write(allocator, ctx, function->body)) { return; }
        ctx->is_async = is_async;
        ctx->is_generator = is_generator;
        neo_writer_pop_scope(allocator, ctx, function->node.scope);
      }
    }
    neo_write_scope_t scope = ctx->scope;
    ctx->scope = scope->parent;
    neo_allocator_free(allocator, scope);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_SCOPE);
  }
}

static void neo_write_context_dispose(neo_allocator_t allocator,
                                      neo_write_context_t self) {
  while (self->scope) {
    neo_write_scope_t scope = self->scope;
    self->scope = scope->parent;
  }
}
neo_write_context_t neo_create_write_context(neo_allocator_t allocator,
                                             neo_program_t program) {
  neo_write_context_t ctx =
      neo_allocator_alloc(allocator, sizeof(struct _neo_write_context_t),
                          neo_write_context_dispose);
  ctx->program = program;
  ctx->scope = NULL;
  ctx->label = NULL;
  ctx->is_async = false;
  ctx->is_generator = true;
  return ctx;
}

void neo_write_optional_chain(neo_allocator_t allocator,
                              neo_write_context_t ctx, neo_ast_node_t node,
                              neo_list_t addresses) {
  if (node->type == NEO_NODE_TYPE_EXPRESSION_CALL ||
      node->type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_CALL) {
    neo_ast_expression_call_t call = (neo_ast_expression_call_t)node;
    bool is_eval = false;
    if (call->callee->type == NEO_NODE_TYPE_IDENTIFIER) {
      if (neo_location_is(call->callee->location, "eval")) {
        is_eval = true;
      }
    }
    if (call->callee->type == NEO_NODE_TYPE_EXPRESSION_MEMBER ||
        call->callee->type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
      neo_ast_expression_member_t member =
          (neo_ast_expression_member_t)call->callee;
      if (member->host->type != NEO_NODE_TYPE_EXPRESSION_SUPER) {
        TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
          return;
        }
      }
      if (member->node.type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
        wchar_t *name = neo_location_get(allocator, member->field->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else {
        TRY(member->field->write(allocator, ctx, member->field)) { return; };
      }
    } else if (call->callee->type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER ||
               call->callee->type ==
                   NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER) {
      neo_ast_expression_member_t member =
          (neo_ast_expression_member_t)call->callee;
      if (member->host->type != NEO_NODE_TYPE_EXPRESSION_SUPER) {
        TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
          return;
        }
      }
      size_t *address = neo_allocator_alloc(allocator, sizeof(size_t), NULL);
      neo_list_push(addresses, address);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JNULL);
      *address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
      if (member->node.type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
        wchar_t *name = neo_location_get(allocator, member->field->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else {
        TRY(member->field->write(allocator, ctx, member->field)) { return; };
      }
    } else if (call->callee->type != NEO_NODE_TYPE_EXPRESSION_SUPER) {
      if (!is_eval) {
        TRY(neo_write_optional_chain(allocator, ctx, call->callee, addresses)) {
          return;
        }
      }
    }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
    for (neo_list_node_t it = neo_list_get_first(call->arguments);
         it != neo_list_get_tail(call->arguments);
         it = neo_list_node_next(it)) {
      neo_ast_node_t argument = neo_list_node_get(it);
      if (argument->type != NEO_NODE_TYPE_EXPRESSION_SPREAD) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
        neo_program_add_integer(allocator, ctx->program, 1);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_program_add_string(allocator, ctx->program, L"length");
        neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
        TRY(argument->write(allocator, ctx, argument)) { return; }
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
      } else {
        TRY(argument->write(allocator, ctx, argument)) { return; }
      }
    }
    if (node->type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_CALL) {
      size_t *address = neo_allocator_alloc(allocator, sizeof(size_t), NULL);
      neo_list_push(addresses, address);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JNULL);
      *address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
    }

    if (call->callee->type == NEO_NODE_TYPE_EXPRESSION_MEMBER ||
        call->callee->type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER ||
        call->callee->type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER ||
        call->callee->type ==
            NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER) {

      neo_ast_expression_member_t member =
          (neo_ast_expression_member_t)call->callee;
      if (member->host->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
        neo_program_add_code(allocator, ctx->program,
                             NEO_ASM_SUPER_MEMBER_CALL);
      } else {
        if (member->field->type == NEO_NODE_TYPE_PRIVATE_NAME) {
          neo_program_add_code(allocator, ctx->program,
                               NEO_ASM_PRIVATE_MEMBER_CALL);
        } else {
          neo_program_add_code(allocator, ctx->program, NEO_ASM_MEMBER_CALL);
        }
      }
    } else if (call->callee->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_SUPER_CALL);
    } else if (is_eval) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_EVAL);
    } else {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_CALL);
    }
    neo_program_add_integer(allocator, ctx->program, node->location.begin.line);
    neo_program_add_integer(allocator, ctx->program,
                            node->location.begin.column);
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
    neo_ast_expression_member_t member = (neo_ast_expression_member_t)node;
    if (member->host->type != NEO_NODE_TYPE_EXPRESSION_SUPER) {
      TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
        return;
      }
    }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
    wchar_t *field = neo_location_get(allocator, member->field->location);
    neo_program_add_string(allocator, ctx->program, field);
    neo_allocator_free(allocator, field);
    if (member->host->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_SUPER_FIELD);
    } else {
      if (member->field->type == NEO_NODE_TYPE_PRIVATE_NAME) {
        neo_program_add_code(allocator, ctx->program,
                             NEO_ASM_GET_PRIVATE_FIELD);
      } else {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
      }
    }
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
    neo_ast_expression_member_t member = (neo_ast_expression_member_t)node;

    if (member->host->type != NEO_NODE_TYPE_EXPRESSION_SUPER) {
      TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
        return;
      }
    }
    TRY(member->field->write(allocator, ctx, member->field)) { return; }
    if (member->host->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_SUPER_FIELD);
    } else {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
    }
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER) {
    neo_ast_expression_member_t member = (neo_ast_expression_member_t)node;
    if (member->host->type != NEO_NODE_TYPE_EXPRESSION_SUPER) {
      TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
        return;
      }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JNULL);
      size_t *address = neo_allocator_alloc(allocator, sizeof(size_t), NULL);
      *address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
      neo_list_push(addresses, address);
    }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
    wchar_t *field = neo_location_get(allocator, member->field->location);
    neo_program_add_string(allocator, ctx->program, field);
    neo_allocator_free(allocator, field);
    if (member->host->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_SUPER_FIELD);
    } else {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
    }
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER) {
    neo_ast_expression_member_t member = (neo_ast_expression_member_t)node;
    if (member->host->type != NEO_NODE_TYPE_EXPRESSION_SUPER) {
      TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
        return;
      }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JNULL);
      size_t *address = neo_allocator_alloc(allocator, sizeof(size_t), NULL);
      *address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
      neo_list_push(addresses, address);
    }
    TRY(member->field->write(allocator, ctx, member->field)) { return; }
    if (member->host->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_SUPER_FIELD);
    } else {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
    }
  } else {
    if (node->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)",
            ctx->program->filename, node->location.begin.line,
            node->location.begin.column);
      return;
    }
    TRY(node->write(allocator, ctx, node)) { return; }
  }
}