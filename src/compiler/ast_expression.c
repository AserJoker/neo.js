#include "compiler/asm.h"
#include "compiler/ast_expression.h"
#include "compiler/ast_expression_array.h"
#include "compiler/ast_expression_arrow_function.h"
#include "compiler/ast_expression_assigment.h"
#include "compiler/ast_expression_call.h"
#include "compiler/ast_expression_class.h"
#include "compiler/ast_expression_condition.h"
#include "compiler/ast_expression_function.h"
#include "compiler/ast_expression_group.h"
#include "compiler/ast_expression_member.h"
#include "compiler/ast_expression_new.h"
#include "compiler/ast_expression_object.h"
#include "compiler/ast_expression_super.h"
#include "compiler/ast_expression_this.h"
#include "compiler/ast_expression_yield.h"
#include "compiler/ast_identifier.h"
#include "compiler/ast_literal_boolean.h"
#include "compiler/ast_literal_null.h"
#include "compiler/ast_literal_numeric.h"
#include "compiler/ast_literal_regexp.h"
#include "compiler/ast_literal_string.h"
#include "compiler/ast_literal_template.h"
#include "compiler/ast_literal_undefined.h"
#include "compiler/ast_node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/buffer.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>


static void
neo_ast_expression_binary_dispose(neo_allocator_t allocator,
                                  neo_ast_expression_binary_t node) {
  neo_allocator_free(allocator, node->left);
  neo_allocator_free(allocator, node->right);
  neo_allocator_free(allocator, node->opt);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_expression_binary_resolve_closure(neo_allocator_t allocator,
                                          neo_ast_expression_binary_t self,
                                          neo_list_t closure) {
  if (self->left) {
    self->left->resolve_closure(allocator, self->left, closure);
  }
  if (self->right) {
    self->right->resolve_closure(allocator, self->right, closure);
  }
}

static neo_any_t
neo_serialize_ast_expression_binary(neo_allocator_t allocator,
                                    neo_ast_expression_binary_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPRESSION_BINARY"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "left", neo_ast_node_serialize(allocator, node->left));
  neo_any_set(variable, "right",
              neo_ast_node_serialize(allocator, node->right));

  char *opt = neo_location_get(allocator, node->opt->location);
  neo_any_set(variable, "operator", neo_create_any_string(allocator, opt));
  neo_allocator_free(allocator, opt);
  return variable;
}

static void neo_ast_expression_binary_write(neo_allocator_t allocator,
                                            neo_write_context_t ctx,
                                            neo_ast_expression_binary_t self) {
  if (!self->left) {
    if (neo_location_is(self->opt->location, "delete")) {
      if (self->right->type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
        neo_ast_expression_member_t member =
            (neo_ast_expression_member_t)self->right;
        member->host->write(allocator, ctx, member->host);
        char *field = neo_location_get(allocator, member->field->location);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_js_program_add_string(allocator, ctx->program, field);
        neo_allocator_free(allocator, field);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DEL_FIELD);
      } else if (self->right->type ==
                 NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
        neo_ast_expression_member_t member =
            (neo_ast_expression_member_t)self->right;
        member->host->write(allocator, ctx, member->host);
        member->field->write(allocator, ctx, member->field);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DEL_FIELD);
      } else {
        self->right->write(allocator, ctx, self->right);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DEL);
      }
    } else {
      self->right->write(allocator, ctx, self->right);
      if (neo_location_is(self->opt->location, "await")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_AWAIT);
      } else if (neo_location_is(self->opt->location, "void")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_VOID);
      } else if (neo_location_is(self->opt->location, "typeof")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_TYPEOF);
      } else if (neo_location_is(self->opt->location, "++")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_INC);
      } else if (neo_location_is(self->opt->location, "--")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DEC);
      } else if (neo_location_is(self->opt->location, "+")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PLUS);
      } else if (neo_location_is(self->opt->location, "-")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NEG);
      } else if (neo_location_is(self->opt->location, "!")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_LOGICAL_NOT);
      } else if (neo_location_is(self->opt->location, "~")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NOT);
      }
    }
  } else if (!self->right) {
    self->left->write(allocator, ctx, self->left);
    if (neo_location_is(self->opt->location, "++")) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DEFER_INC);
    } else if (neo_location_is(self->opt->location, "--")) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DEFER_DEC);
    }
  } else {
    self->left->write(allocator, ctx, self->left);
    if (neo_location_is(self->opt->location, ",")) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      self->right->write(allocator, ctx, self->right);
    } else if (neo_location_is(self->opt->location, "??")) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JNOT_NULL);
      size_t address = neo_buffer_get_size(ctx->program->codes);
      neo_js_program_add_address(allocator, ctx->program, 0);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      self->right->write(allocator, ctx, self->right);
      neo_js_program_set_current(ctx->program, address);
    } else if (neo_location_is(self->opt->location, "||")) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JTRUE);
      size_t address = neo_buffer_get_size(ctx->program->codes);
      neo_js_program_add_address(allocator, ctx->program, 0);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      self->right->write(allocator, ctx, self->right);
      neo_js_program_set_current(ctx->program, address);
    } else if (neo_location_is(self->opt->location, "&&")) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JFALSE);
      size_t address = neo_buffer_get_size(ctx->program->codes);
      neo_js_program_add_address(allocator, ctx->program, 0);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      self->right->write(allocator, ctx, self->right);
      neo_js_program_set_current(ctx->program, address);
    } else {
      self->right->write(allocator, ctx, self->right);
      if (neo_location_is(self->opt->location, "+")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_ADD);
      } else if (neo_location_is(self->opt->location, "-")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SUB);
      } else if (neo_location_is(self->opt->location, "*")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_MUL);
      } else if (neo_location_is(self->opt->location, "/")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DIV);
      } else if (neo_location_is(self->opt->location, "%")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_MOD);
      } else if (neo_location_is(self->opt->location, "**")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POW);
      } else if (neo_location_is(self->opt->location, "&")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_AND);
      } else if (neo_location_is(self->opt->location, "|")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_OR);
      } else if (neo_location_is(self->opt->location, "^")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_XOR);
      } else if (neo_location_is(self->opt->location, ">>")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SHR);
      } else if (neo_location_is(self->opt->location, "<<")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SHL);
      } else if (neo_location_is(self->opt->location, ">>>")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_USHR);
      } else if (neo_location_is(self->opt->location, ">")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_GT);
      } else if (neo_location_is(self->opt->location, ">=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_GE);
      } else if (neo_location_is(self->opt->location, "<")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_LT);
      } else if (neo_location_is(self->opt->location, "<=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_LE);
      } else if (neo_location_is(self->opt->location, "==")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_EQ);
      } else if (neo_location_is(self->opt->location, "!=")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NE);
      } else if (neo_location_is(self->opt->location, "===")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SEQ);
      } else if (neo_location_is(self->opt->location, "!==")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SNE);
      } else if (neo_location_is(self->opt->location, "in")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_IN);
      } else if (neo_location_is(self->opt->location, "instanceof")) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_INSTANCE_OF);
      }
    }
  }
}

static neo_ast_expression_binary_t
neo_create_ast_expression_binary(neo_allocator_t allocator) {
  neo_ast_expression_binary_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_expression_binary_t),
      neo_ast_expression_binary_dispose);
  node->left = NULL;
  node->right = NULL;
  node->opt = NULL;
  node->node.type = NEO_NODE_TYPE_EXPRESSION_BINARY;

  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_binary;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_binary_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_expression_binary_write;
  return node;
}

neo_ast_node_t neo_ast_read_expression_19(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = neo_ast_read_literal_string(allocator, file, position);
  if (!node) {
    node = neo_ast_read_literal_numeric(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_literal_null(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_literal_undefined(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_literal_boolean(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_literal_regexp(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_array(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_object(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_literal_template(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_function(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_class(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_this(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_super(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_identifier(allocator, file, position);
  }
  return node;
}

neo_ast_node_t neo_ast_read_expression_18(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  node = neo_ast_read_expression_group(allocator, file, position);
  if (!node) {
    node = neo_ast_read_expression_19(allocator, file, position);
  }
  return node;
}

neo_ast_node_t neo_ast_read_expression_17(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  node = neo_ast_read_expression_18(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror);
    neo_position_t cur = current;
    error = neo_skip_all(allocator, file, &cur);
    if (error) {
      goto onerror;
    }
    for (;;) {
      neo_ast_node_t bnode = NULL;
      bnode = neo_ast_read_expression_member(allocator, file, &cur);
      if (bnode) {
        NEO_CHECK_NODE(bnode, error, onerror);
        ((neo_ast_expression_member_t)bnode)->host = node;
      }
      if (!bnode) {
        bnode = neo_ast_read_expression_call(allocator, file, &cur);
        if (bnode) {
          NEO_CHECK_NODE(bnode, error, onerror);
          ((neo_ast_expression_call_t)bnode)->callee = node;
        }
      }
      if (!bnode) {
        bnode = neo_ast_read_literal_template(allocator, file, &cur);
        if (bnode) {
          NEO_CHECK_NODE(bnode, error, onerror);
          ((neo_ast_literal_template_t)bnode)->tag = node;
        }
      }
      if (!bnode) {
        break;
      }
      node = bnode;
      node->location.begin = *position;
      node->location.end = cur;
      node->location.file = file;
      current = cur;
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_expression_16(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  node = neo_ast_read_expression_new(allocator, file, position);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror);
    neo_position_t current = *position;
    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    for (;;) {
      neo_ast_node_t bnode = NULL;
      bnode = neo_ast_read_expression_member(allocator, file, &current);
      if (bnode) {
        NEO_CHECK_NODE(bnode, error, onerror);
        ((neo_ast_expression_member_t)bnode)->host = node;
      }
      if (!bnode) {
        bnode = neo_ast_read_expression_call(allocator, file, &current);
        if (bnode) {
          NEO_CHECK_NODE(bnode, error, onerror);
          ((neo_ast_expression_call_t)bnode)->callee = node;
        }
      }
      if (!bnode) {
        bnode = neo_ast_read_literal_template(allocator, file, &current);
        if (bnode) {
          NEO_CHECK_NODE(bnode, error, onerror);
          ((neo_ast_literal_template_t)bnode)->tag = node;
        }
      }
      if (!bnode) {
        break;
      }
      node = bnode;
      node->location.begin = *position;
      node->location.end = current;
      node->location.file = file;
      *position = current;
    }
  }
  if (!node) {
    node = neo_ast_read_expression_17(allocator, file, position);
    if (node) {
      NEO_CHECK_NODE(node, error, onerror);
    }
  }
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_expression_15(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_16(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror);
    neo_position_t cur = current;
    error = neo_skip_all(allocator, file, &cur);
    if (error) {
      goto onerror;
    }
    if (cur.line == current.line) {
      token = neo_read_symbol_token(allocator, file, &cur);
      if (token && (neo_location_is(token->location, "++") ||
                    neo_location_is(token->location, "--"))) {
        neo_ast_expression_binary_t bnode =
            neo_create_ast_expression_binary(allocator);
        bnode->opt = token;
        bnode->left = node;
        bnode->node.location.begin = *position;
        bnode->node.location.end = cur;
        bnode->node.location.file = file;
        *position = cur;
        return &bnode->node;
      } else {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_expression_14(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_token_t token = neo_read_symbol_token(allocator, file, &current);
  if (!token) {
    token = neo_read_identify_token(allocator, file, &current);
  }
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (token && (neo_location_is(token->location, "~") ||
                neo_location_is(token->location, "!") ||
                neo_location_is(token->location, "+") ||
                neo_location_is(token->location, "-") ||
                neo_location_is(token->location, "++") ||
                neo_location_is(token->location, "--") ||
                neo_location_is(token->location, "typeof") ||
                neo_location_is(token->location, "void") ||
                neo_location_is(token->location, "delete") ||
                neo_location_is(token->location, "await"))) {
    if (neo_location_is(token->location, "await") &&
        !neo_compile_scope_is_async()) {
      if (!neo_compile_scope_is_async()) {
        error = neo_create_error_node(allocator,
                                      "await only used in generator context");
        goto onerror;
      }
    }
    neo_ast_expression_binary_t bnode =
        neo_create_ast_expression_binary(allocator);
    node = &bnode->node;
    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    bnode->opt = token;
    token = NULL;
    bnode->right = neo_ast_read_expression_14(allocator, file, &current);
    if (!bnode->right) {
      node = &bnode->node;
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    if (bnode->right->type == NEO_NODE_TYPE_ERROR) {
      node = &bnode->node;
      error = bnode->right;
      bnode->right = NULL;
      goto onerror;
    }
    bnode->node.location.begin = *position;
    bnode->node.location.end = current;
    bnode->node.location.file = file;
    *position = current;
  } else {
    if (token) {
      neo_allocator_free(allocator, token);
    }
  }
  if (!node) {
    node = neo_ast_read_expression_15(allocator, file, position);
  }
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_expression_13(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_14(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror)
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "**"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_13(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_expression_12(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_13(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror)
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "*") ||
                  neo_location_is(token->location, "/") ||
                  neo_location_is(token->location, "%"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_12(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}
neo_ast_node_t neo_ast_read_expression_11(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_12(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror)
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "+") ||
                  neo_location_is(token->location, "-"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_11(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}
neo_ast_node_t neo_ast_read_expression_10(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_11(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror)
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, ">>") ||
                  neo_location_is(token->location, "<<") ||
                  neo_location_is(token->location, ">>>"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_10(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_expression_9(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_10(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror)
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    if (!token) {
      token = neo_read_identify_token(allocator, file, &curr);
    }
    if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
      error = neo_create_error_node(allocator, NULL);
      error->error = token->error;
      token->error = NULL;
      goto onerror;
    }
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "<") ||
                  neo_location_is(token->location, "<=") ||
                  neo_location_is(token->location, ">") ||
                  neo_location_is(token->location, ">=") ||
                  neo_location_is(token->location, "in") ||
                  neo_location_is(token->location, "instanceof"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_9(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}
neo_ast_node_t neo_ast_read_expression_8(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_9(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror);
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "==") ||
                  neo_location_is(token->location, "!=") ||
                  neo_location_is(token->location, "===") ||
                  neo_location_is(token->location, "!=="))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_8(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}
neo_ast_node_t neo_ast_read_expression_7(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_8(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror)
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "&"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_7(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}
neo_ast_node_t neo_ast_read_expression_6(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_7(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror);
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "^"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_6(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
      return &bnode->node;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}
neo_ast_node_t neo_ast_read_expression_5(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_6(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror)
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "|"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_5(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}
neo_ast_node_t neo_ast_read_expression_4(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_5(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror);
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "&&"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_4(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}
neo_ast_node_t neo_ast_read_expression_3(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = neo_ast_read_expression_4(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror)
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    neo_token_t token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "||") ||
                  neo_location_is(token->location, "??"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_3(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      NEO_CHECK_NODE(bnode->right, error, onerror);
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      if (token) {
        neo_allocator_free(allocator, token);
      }
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_expression_2(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_ast_node_t node = NULL;
  if (!node) {
    node = neo_ast_read_expression_yield(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_arrow_function(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_assigment(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_condition(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_expression_3(allocator, file, position);
  }
  return node;
}

static neo_ast_node_t
neo_ast_read_expression_sequence(neo_allocator_t allocator, const char *file,
                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_node_t error = NULL;
  neo_ast_node_t node = neo_ast_read_expression_2(allocator, file, &current);
  if (node) {
    NEO_CHECK_NODE(node, error, onerror);
    neo_position_t curr = current;
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    token = neo_read_symbol_token(allocator, file, &curr);
    error = neo_skip_all(allocator, file, &curr);
    if (error) {
      goto onerror;
    }
    if (token && neo_location_is(token->location, ",")) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      node = &bnode->node;
      bnode->opt = token;
      bnode->right = neo_ast_read_expression_sequence(allocator, file, &curr);
      if (!bnode->right) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            curr.line, curr.column);
        goto onerror;
      }
      if (bnode->right) {
        NEO_CHECK_NODE(bnode->right, error, onerror);
      }
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
    } else {
      neo_allocator_free(allocator, token);
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_expression_1(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  return neo_ast_read_expression_sequence(allocator, file, position);
}