#include "compiler/expression.h"
#include "compiler/expression_array.h"
#include "compiler/expression_arrow_function.h"
#include "compiler/expression_assigment.h"
#include "compiler/expression_call.h"
#include "compiler/expression_class.h"
#include "compiler/expression_condition.h"
#include "compiler/expression_function.h"
#include "compiler/expression_group.h"
#include "compiler/expression_member.h"
#include "compiler/expression_new.h"
#include "compiler/expression_object.h"
#include "compiler/expression_super.h"
#include "compiler/expression_this.h"
#include "compiler/expression_yield.h"
#include "compiler/identifier.h"
#include "compiler/literal_boolean.h"
#include "compiler/literal_null.h"
#include "compiler/literal_numeric.h"
#include "compiler/literal_string.h"
#include "compiler/literal_template.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include <stdbool.h>
#include <stdio.h>

static void
neo_ast_expression_binary_dispose(neo_allocator_t allocator,
                                  neo_ast_expression_binary_t self) {
  if (self->left) {
    neo_allocator_free(allocator, self->left);
  }
  if (self->right) {
    neo_allocator_free(allocator, self->right);
  }
  if (self->opt) {
    neo_allocator_free(allocator, self->opt);
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
  return node;
}

neo_ast_node_t neo_ast_read_expression_19(neo_allocator_t allocator,
                                          const char *file,
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
                                          const char *file,
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
                                          const char *file,
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
      SKIP_ALL(allocator, file, &cur, onerror);
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
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_node_t node = NULL;
  node = TRY(neo_ast_read_expression_new(allocator, file, position)) {
    goto onerror;
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
                                          const char *file,
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
                                          const char *file,
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
    neo_ast_expression_binary_t bnode =
        neo_create_ast_expression_binary(allocator);
    SKIP_ALL(allocator, file, &current, onerror);
    bnode->opt = token;
    bnode->right = TRY(neo_ast_read_expression_14(allocator, file, &current)) {
      goto onerror;
    };
    if (!bnode->right) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
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
                                          const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                          const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                          const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                          const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                         const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
        goto onerror;
      }
      bnode->node.location.begin = *position;
      bnode->node.location.end = curr;
      bnode->node.location.file = file;
      *position = current;
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
                                         const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
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
                                         const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                         const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                         const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                         const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                         const char *file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, curr.line, curr.column);
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
                                         const char *file,
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
neo_ast_read_expression_sequence(neo_allocator_t allocator, const char *file,
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
                                         const char *file,
                                         neo_position_t *position) {
  return neo_ast_read_expression_sequence(allocator, file, position);
}