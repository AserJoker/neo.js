#include "compiler/ast/expression.h"
#include "compiler/asm.h"
#include "compiler/ast/expression_array.h"
#include "compiler/ast/expression_arrow_function.h"
#include "compiler/ast/expression_assigment.h"
#include "compiler/ast/expression_call.h"
#include "compiler/ast/expression_class.h"
#include "compiler/ast/expression_condition.h"
#include "compiler/ast/expression_function.h"
#include "compiler/ast/expression_group.h"
#include "compiler/ast/expression_member.h"
#include "compiler/ast/expression_new.h"
#include "compiler/ast/expression_object.h"
#include "compiler/ast/expression_super.h"
#include "compiler/ast/expression_this.h"
#include "compiler/ast/expression_yield.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/literal_boolean.h"
#include "compiler/ast/literal_null.h"
#include "compiler/ast/literal_numeric.h"
#include "compiler/ast/literal_string.h"
#include "compiler/ast/literal_template.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
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

static neo_variable_t
neo_serialize_ast_expression_binary(neo_allocator_t allocator,
                                    neo_ast_expression_binary_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_BINARY"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "left",
                   neo_ast_node_serialize(allocator, node->left));
  neo_variable_set(variable, "right",
                   neo_ast_node_serialize(allocator, node->right));
  char opt[16];
  size_t size =
      node->opt->location.end.offset - node->opt->location.begin.offset;
  strncpy(opt, node->opt->location.begin.offset, size);
  opt[size] = 0;
  neo_variable_set(variable, "operator",
                   neo_create_variable_string(allocator, opt));
  return variable;
}

static void neo_ast_expression_binary_write(neo_allocator_t allocator,
                                            neo_write_context_t ctx,
                                            neo_ast_expression_binary_t self) {
  if (!self->left) {
    TRY(self->right->write(allocator, ctx, self->right)) { return; }
    if (neo_location_is(self->opt->location, "await")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_AWAIT);
    } else if (neo_location_is(self->opt->location, "delete")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_DEL);
    } else if (neo_location_is(self->opt->location, "void")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_VOID);
    } else if (neo_location_is(self->opt->location, "typeof")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_TYPEOF);
    } else if (neo_location_is(self->opt->location, "++")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_INC);
      if (self->right->type == NEO_NODE_TYPE_IDENTIFIER) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
        neo_program_add_integer(allocator, ctx->program, 1);
        char *name = neo_location_get(allocator, self->right->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else if (self->right->type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
        neo_ast_expression_member_t member =
            (neo_ast_expression_member_t)self->right;
        neo_list_initialize_t initialize = {true};
        neo_list_t addresses = neo_create_list(allocator, &initialize);
        TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
          neo_allocator_free(allocator, addresses);
          return;
        }
        if (neo_list_get_size(addresses)) {
          neo_allocator_free(allocator, addresses);
          THROW("Invalid left-hand side expression in postfix operation");
          return;
        }
        neo_allocator_free(allocator, addresses);
        char *name = neo_location_get(allocator, member->field->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
        neo_program_add_integer(allocator, ctx->program, 3);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      } else if (self->right->type ==
                 NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
        neo_ast_expression_member_t member =
            (neo_ast_expression_member_t)self->right;
        neo_list_initialize_t initialize = {true};
        neo_list_t addresses = neo_create_list(allocator, &initialize);
        TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
          neo_allocator_free(allocator, addresses);
          return;
        }
        if (neo_list_get_size(addresses)) {
          neo_allocator_free(allocator, addresses);
          THROW("Invalid left-hand side expression in postfix operation");
          return;
        }
        neo_allocator_free(allocator, addresses);
        TRY(member->field->write(allocator, ctx, member->field)) { return; }
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
        neo_program_add_integer(allocator, ctx->program, 3);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      } else {
        THROW("Invalid left-hand side expression in postfix operation");
        return;
      }
    } else if (neo_location_is(self->opt->location, "--")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_DEC);
      if (self->right->type == NEO_NODE_TYPE_IDENTIFIER) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
        neo_program_add_integer(allocator, ctx->program, 1);
        char *name = neo_location_get(allocator, self->right->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else if (self->right->type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
        neo_ast_expression_member_t member =
            (neo_ast_expression_member_t)self->right;
        neo_list_initialize_t initialize = {true};
        neo_list_t addresses = neo_create_list(allocator, &initialize);
        TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
          neo_allocator_free(allocator, addresses);
          return;
        }
        if (neo_list_get_size(addresses)) {
          neo_allocator_free(allocator, addresses);
          THROW("Invalid left-hand side expression in postfix operation");
          return;
        }
        neo_allocator_free(allocator, addresses);
        char *name = neo_location_get(allocator, member->field->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
        neo_program_add_integer(allocator, ctx->program, 3);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      } else if (self->right->type ==
                 NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
        neo_ast_expression_member_t member =
            (neo_ast_expression_member_t)self->right;
        neo_list_initialize_t initialize = {true};
        neo_list_t addresses = neo_create_list(allocator, &initialize);
        TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
          neo_allocator_free(allocator, addresses);
          return;
        }
        if (neo_list_get_size(addresses)) {
          neo_allocator_free(allocator, addresses);
          THROW("Invalid left-hand side expression in postfix operation");
          return;
        }
        neo_allocator_free(allocator, addresses);
        TRY(member->field->write(allocator, ctx, member->field)) { return; }
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
        neo_program_add_integer(allocator, ctx->program, 3);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      } else {
        THROW("Invalid left-hand side expression in postfix operation");
        return;
      }
    } else if (neo_location_is(self->opt->location, "+")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PLUS);
    } else if (neo_location_is(self->opt->location, "-")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_NEG);
    } else if (neo_location_is(self->opt->location, "!")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_LOGICAL_NOT);
    } else if (neo_location_is(self->opt->location, "~")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_NOT);
    }
  } else if (!self->right) {
    TRY(self->left->write(allocator, ctx, self->left)) { return; }
    if (self->left->type == NEO_NODE_TYPE_IDENTIFIER) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 1);
      if (neo_location_is(self->opt->location, "++")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_INC);
      } else if (neo_location_is(self->opt->location, "--")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_DEC);
      }
      char *name = neo_location_get(allocator, self->left->location);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
      neo_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
    } else if (self->left->type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
      neo_ast_expression_member_t member =
          (neo_ast_expression_member_t)self->left;
      neo_list_initialize_t initialize = {true};
      neo_list_t addresses = neo_create_list(allocator, &initialize);
      TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
        neo_allocator_free(allocator, addresses);
        return;
      }
      if (neo_list_get_size(addresses)) {
        neo_allocator_free(allocator, addresses);
        THROW("Invalid left-hand side expression in postfix operation");
        return;
      }
      neo_allocator_free(allocator, addresses);
      char *name = neo_location_get(allocator, member->field->location);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      neo_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 3);
      if (neo_location_is(self->opt->location, "++")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_INC);
      } else if (neo_location_is(self->opt->location, "--")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_DEC);
      }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    } else if (self->left->type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
      neo_ast_expression_member_t member =
          (neo_ast_expression_member_t)self->left;
      neo_list_initialize_t initialize = {true};
      neo_list_t addresses = neo_create_list(allocator, &initialize);
      TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
        neo_allocator_free(allocator, addresses);
        return;
      }
      if (neo_list_get_size(addresses)) {
        neo_allocator_free(allocator, addresses);
        THROW("Invalid left-hand side expression in postfix operation");
        return;
      }
      neo_allocator_free(allocator, addresses);
      TRY(member->field->write(allocator, ctx, member->field)) { return; }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 3);
      if (neo_location_is(self->opt->location, "++")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_INC);
      } else if (neo_location_is(self->opt->location, "--")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_DEC);
      }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    } else {
      THROW("Invalid left-hand side expression in postfix operation");
      return;
    }
  } else {
    TRY(self->left->write(allocator, ctx, self->left)) { return; }
    if (neo_location_is(self->opt->location, ",")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      TRY(self->right->write(allocator, ctx, self->right)) { return; }
    } else if (neo_location_is(self->opt->location, "??")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JNOT_NULL);
      size_t address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      TRY(self->right->write(allocator, ctx, self->right)) { return; }
      neo_program_set_current(ctx->program, address);
    } else if (neo_location_is(self->opt->location, "||")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JTRUE);
      size_t address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      TRY(self->right->write(allocator, ctx, self->right)) { return; }
      neo_program_set_current(ctx->program, address);
    } else if (neo_location_is(self->opt->location, "&&")) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JFALSE);
      size_t address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      TRY(self->right->write(allocator, ctx, self->right)) { return; }
      neo_program_set_current(ctx->program, address);
    } else {
      TRY(self->right->write(allocator, ctx, self->right)) { return; }
      if (neo_location_is(self->opt->location, "+")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_ADD);
      } else if (neo_location_is(self->opt->location, "-")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SUB);
      } else if (neo_location_is(self->opt->location, "*")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_MUL);
      } else if (neo_location_is(self->opt->location, "/")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_DIV);
      } else if (neo_location_is(self->opt->location, "%")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_MOD);
      } else if (neo_location_is(self->opt->location, "**")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_POW);
      } else if (neo_location_is(self->opt->location, "&")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_AND);
      } else if (neo_location_is(self->opt->location, "|")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_OR);
      } else if (neo_location_is(self->opt->location, "^")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_XOR);
      } else if (neo_location_is(self->opt->location, ">>")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SHR);
      } else if (neo_location_is(self->opt->location, "<<")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SHL);
      } else if (neo_location_is(self->opt->location, ">>>")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_USHR);
      } else if (neo_location_is(self->opt->location, ">")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_GT);
      } else if (neo_location_is(self->opt->location, ">=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_GE);
      } else if (neo_location_is(self->opt->location, "<")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_LT);
      } else if (neo_location_is(self->opt->location, "<=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_LE);
      } else if (neo_location_is(self->opt->location, "==")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_EQ);
      } else if (neo_location_is(self->opt->location, "!=")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_NE);
      } else if (neo_location_is(self->opt->location, "===")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SEQ);
      } else if (neo_location_is(self->opt->location, "!==")) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_SNE);
      }
    }
  }
}

static neo_ast_expression_binary_t
neo_create_ast_expression_binary(neo_allocator_t allocator) {
  neo_ast_expression_binary_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_binary);
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
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node =
      TRY(neo_ast_read_literal_string(allocator, file, position)) {
    goto onerror;
  };
  if (!node) {
    node = TRY(neo_ast_read_literal_numeric(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_literal_null(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_literal_boolean(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_array(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_object(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_literal_template(allocator, file, position)) {
      goto onerror;
    };
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_function(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_class(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_this(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_super(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_identifier(allocator, file, position)) {
      goto onerror;
    }
  }
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}

neo_ast_node_t neo_ast_read_expression_18(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  node = TRY(neo_ast_read_expression_group(allocator, file, position)) {
    goto onerror;
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_19(allocator, file, position)) {
      goto onerror;
    };
  }
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}

neo_ast_node_t neo_ast_read_expression_17(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_18(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t cur = current;
    SKIP_ALL(allocator, file, &cur, onerror);
    for (;;) {
      neo_ast_node_t bnode = NULL;
      bnode = TRY(neo_ast_read_expression_member(allocator, file, &cur)) {
        goto onerror;
      };
      if (bnode) {
        ((neo_ast_expression_member_t)bnode)->host = node;
      }
      if (!bnode) {
        bnode = TRY(neo_ast_read_expression_call(allocator, file, &cur)) {
          goto onerror;
        };
        if (bnode) {
          ((neo_ast_expression_call_t)bnode)->callee = node;
        }
      }
      if (!bnode) {
        bnode = TRY(neo_ast_read_literal_template(allocator, file, &cur)) {
          goto onerror;
        };
        if (bnode) {
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
  return NULL;
}

neo_ast_node_t neo_ast_read_expression_16(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  node = TRY(neo_ast_read_expression_new(allocator, file, position)) {
    goto onerror;
  }
  if (node) {
    neo_position_t current = *position;
    SKIP_ALL(allocator, file, &current, onerror);
    for (;;) {
      neo_ast_node_t bnode = NULL;
      bnode = TRY(neo_ast_read_expression_member(allocator, file, &current)) {
        goto onerror;
      };
      if (bnode) {
        ((neo_ast_expression_member_t)bnode)->host = node;
      }
      if (!bnode) {
        bnode = TRY(neo_ast_read_expression_call(allocator, file, &current)) {
          goto onerror;
        };
        if (bnode) {
          ((neo_ast_expression_call_t)bnode)->callee = node;
        }
      }
      if (!bnode) {
        bnode = TRY(neo_ast_read_literal_template(allocator, file, &current)) {
          goto onerror;
        };
        if (bnode) {
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
    node = TRY(neo_ast_read_expression_17(allocator, file, position)) {
      goto onerror;
    };
  }
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}

neo_ast_node_t neo_ast_read_expression_15(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_16(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t cur = current;
    SKIP_ALL(allocator, file, &cur, onerror);
    if (cur.line == current.line) {
      token = TRY(neo_read_symbol_token(allocator, file, &cur)) {
        goto onerror;
      }
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
  return NULL;
}

neo_ast_node_t neo_ast_read_expression_14(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_position_t current = *position;
  neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token) {
    token = neo_read_identify_token(allocator, file, &current);
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
        THROW("await only used in generator context");
        goto onerror;
      }
    }
    neo_ast_expression_binary_t bnode =
        neo_create_ast_expression_binary(allocator);
    SKIP_ALL(allocator, file, &current, onerror);
    bnode->opt = token;
    bnode->right = TRY(neo_ast_read_expression_14(allocator, file, &current)) {
      goto onerror;
    };
    if (!bnode->right) {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
    bnode->node.location.begin = *position;
    bnode->node.location.end = current;
    bnode->node.location.file = file;
    *position = current;
    return &bnode->node;
  } else {
    if (token) {
      neo_allocator_free(allocator, token);
    }
  }
  node = TRY(neo_ast_read_expression_15(allocator, file, position)) {
    goto onerror;
  };
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}

neo_ast_node_t neo_ast_read_expression_13(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_14(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "**"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_13(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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

neo_ast_node_t neo_ast_read_expression_12(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_13(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "*") ||
                  neo_location_is(token->location, "/") ||
                  neo_location_is(token->location, "%"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_12(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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
neo_ast_node_t neo_ast_read_expression_11(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_12(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "+") ||
                  neo_location_is(token->location, "-"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_11(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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
neo_ast_node_t neo_ast_read_expression_10(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_11(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, ">>") ||
                  neo_location_is(token->location, "<<") ||
                  neo_location_is(token->location, ">>>"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_10(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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

neo_ast_node_t neo_ast_read_expression_9(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_10(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    if (!token) {
      token = TRY(neo_read_identify_token(allocator, file, &curr)) {
        goto onerror;
      }
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "<") ||
                  neo_location_is(token->location, "<=") ||
                  neo_location_is(token->location, ">") ||
                  neo_location_is(token->location, ">=") ||
                  neo_location_is(token->location, "in") ||
                  neo_location_is(token->location, "instanceof"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_9(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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
neo_ast_node_t neo_ast_read_expression_8(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {
  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_9(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "==") ||
                  neo_location_is(token->location, "!=") ||
                  neo_location_is(token->location, "===") ||
                  neo_location_is(token->location, "!=="))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_8(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              current.line, current.column);
        goto onerror;
      }
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
neo_ast_node_t neo_ast_read_expression_7(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_8(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "&"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_7(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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
neo_ast_node_t neo_ast_read_expression_6(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_7(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "^"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_6(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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
                                         const wchar_t *file,
                                         neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_6(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "|"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_5(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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
neo_ast_node_t neo_ast_read_expression_4(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_5(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "&&"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_4(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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
neo_ast_node_t neo_ast_read_expression_3(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {

  neo_ast_node_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  node = TRY(neo_ast_read_expression_4(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    neo_token_t token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && (neo_location_is(token->location, "||") ||
                  neo_location_is(token->location, "??"))) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right = TRY(neo_ast_read_expression_3(allocator, file, &curr)) {
        goto onerror;
      };
      if (!bnode->right) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              curr.line, curr.column);
        goto onerror;
      }
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

neo_ast_node_t neo_ast_read_expression_2(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {
  neo_ast_node_t node = NULL;
  if (!node) {
    node = TRY(neo_ast_read_expression_yield(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_assigment(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node =
        TRY(neo_ast_read_expression_arrow_function(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_condition(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_expression_3(allocator, file, position)) {
      goto onerror;
    };
  }
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}

static neo_ast_node_t
neo_ast_read_expression_sequence(neo_allocator_t allocator, const wchar_t *file,
                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_node_t node =
      TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  };
  if (node) {
    neo_position_t curr = current;
    SKIP_ALL(allocator, file, &curr, onerror);
    token = TRY(neo_read_symbol_token(allocator, file, &curr)) {
      goto onerror;
    };
    SKIP_ALL(allocator, file, &curr, onerror);
    if (token && neo_location_is(token->location, ",")) {
      neo_ast_expression_binary_t bnode =
          neo_create_ast_expression_binary(allocator);
      bnode->left = node;
      bnode->opt = token;
      bnode->right =
          TRY(neo_ast_read_expression_sequence(allocator, file, &curr)) {
        goto onerror;
      }
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = curr;
      return &bnode->node;
    } else {
      neo_allocator_free(allocator, token);
    }
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}

neo_ast_node_t neo_ast_read_expression_1(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {
  return neo_ast_read_expression_sequence(allocator, file, position);
}