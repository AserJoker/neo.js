#include "compiler/ast/literal_template.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/expression_member.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/string.h"
#include "core/variable.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void neo_ast_literal_template_dispose(neo_allocator_t allocator,
                                             neo_ast_literal_template_t node) {
  neo_allocator_free(allocator, node->expressions);
  neo_allocator_free(allocator, node->quasis);
  neo_allocator_free(allocator, node->tag);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_literal_template_resolve_closure(neo_allocator_t allocator,
                                         neo_ast_literal_template_t self,
                                         neo_list_t closure) {
  if (self->tag) {
    self->tag->resolve_closure(allocator, self->tag, closure);
  }
  for (neo_list_node_t it = neo_list_get_first(self->expressions);
       it != neo_list_get_tail(self->expressions);
       it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static void neo_ast_literal_template_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_literal_template_t self) {
  if (self->tag) {
    if (self->tag->type == NEO_NODE_TYPE_EXPRESSION_MEMBER ||
        self->tag->type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
      neo_ast_expression_member_t member =
          (neo_ast_expression_member_t)self->tag;
      neo_list_initialize_t initialize = {true};
      neo_list_t addresses = neo_create_list(allocator, &initialize);
      TRY(neo_write_optional_chain(allocator, ctx, member->host, addresses)) {
        neo_allocator_free(allocator, addresses);
        return;
      }
      if (neo_list_get_size(addresses)) {
        THROW("Invalid tagged template on optional chain \n  at "
              "_.compile (%ls:%d:%d)",
              ctx->program->filename, self->tag->location.begin.line,
              self->tag->location.begin.column);
        neo_allocator_free(allocator, addresses);
        return;
      }
      neo_allocator_free(allocator, addresses);
      if (member->node.type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
        wchar_t *name = neo_location_get(allocator, member->field->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else {
        TRY(member->field->write(allocator, ctx, member->field)) { return; }
      }
    } else {
      neo_list_initialize_t initialize = {true};
      neo_list_t addresses = neo_create_list(allocator, &initialize);
      TRY(neo_write_optional_chain(allocator, ctx, self->tag, addresses)) {
        neo_allocator_free(allocator, addresses);
        return;
      }
      if (neo_list_get_size(addresses)) {
        THROW("Invalid tagged template on optional chain \n  at "
              "_.compile (%ls:%d:%d)",
              ctx->program->filename, self->tag->location.begin.line,
              self->tag->location.begin.column);
        neo_allocator_free(allocator, addresses);
        return;
      }
      neo_allocator_free(allocator, addresses);
    }
    size_t idx = 0;
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_NUMBER);
    neo_program_add_number(allocator, ctx->program, idx);
    size_t count = 0;
    neo_list_node_t qit = neo_list_get_first(self->quasis);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
    while (qit != neo_list_get_tail(self->quasis)) {
      neo_token_t str = neo_list_node_get(qit);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_NUMBER);
      neo_program_add_number(allocator, ctx->program, count);
      wchar_t *s = neo_location_get_raw(allocator, str->location);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      if (str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING ||
          str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_END) {
        s[wcslen(s) - 1] = '\0';
        neo_program_add_string(allocator, ctx->program, s + 1);
      } else if (str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_START ||
                 str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_PART) {
        s[wcslen(s) - 2] = '\0';
        neo_program_add_string(allocator, ctx->program, s + 1);
      }
      neo_allocator_free(allocator, s);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
      qit = neo_list_node_next(qit);
      count++;
    }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
    idx++;
    for (neo_list_node_t it = neo_list_get_first(self->expressions);
         it != neo_list_get_tail(self->expressions);
         it = neo_list_node_next(it)) {
      neo_ast_node_t item = neo_list_node_get(it);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_NUMBER);
      neo_program_add_number(allocator, ctx->program, idx);
      TRY(item->write(allocator, ctx, item)) { return; }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
      idx++;
    }
    if (self->tag->type == NEO_NODE_TYPE_EXPRESSION_MEMBER ||
        self->tag->type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
      neo_ast_expression_member_t member =
          (neo_ast_expression_member_t)(self->tag);
      if (member->field->type == NEO_NODE_TYPE_PRIVATE_NAME) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PRIVATE_TAG);
      } else {
        if (member->host->type == NEO_NODE_TYPE_EXPRESSION_SUPER) {
          neo_program_add_code(allocator, ctx->program,
                               NEO_ASM_SUPER_MEMBER_TAG);
        } else {
          neo_program_add_code(allocator, ctx->program, NEO_ASM_MEMBER_TAG);
        }
      }
    } else {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_TAG);
    }
    neo_program_add_integer(allocator, ctx->program,
                            self->node.location.begin.line);
    neo_program_add_integer(allocator, ctx->program,
                            self->node.location.begin.column);
  } else {
    neo_list_node_t qit = neo_list_get_first(self->quasis);
    neo_token_t str = neo_list_node_get(qit);
    qit = neo_list_node_next(qit);
    wchar_t *s = neo_location_get(allocator, str->location);
    wchar_t *ss = neo_wstring_decode_escape(allocator, s);
    neo_allocator_free(allocator, s);
    s = ss;
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
    if (str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING ||
        str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_END) {
      s[wcslen(s) - 1] = '\0';
      neo_program_add_string(allocator, ctx->program, s + 1);
    } else if (str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_START ||
               str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_PART) {
      s[wcslen(s) - 2] = '\0';
      neo_program_add_string(allocator, ctx->program, s + 1);
    }
    neo_allocator_free(allocator, s);
    for (neo_list_node_t it = neo_list_get_first(self->expressions);
         it != neo_list_get_tail(self->expressions);
         it = neo_list_node_next(it)) {
      neo_ast_node_t item = neo_list_node_get(it);
      TRY(item->write(allocator, ctx, item)) { return; }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_CONCAT);
      str = neo_list_node_get(qit);
      wchar_t *s = neo_location_get(allocator, str->location);
      wchar_t *ss = neo_wstring_decode_escape(allocator, s);
      neo_allocator_free(allocator, s);
      s = ss;
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      if (str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING ||
          str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_END) {
        s[wcslen(s) - 1] = '\0';
        neo_program_add_string(allocator, ctx->program, s + 1);
      } else if (str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_START ||
                 str->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_PART) {
        s[wcslen(s) - 2] = '\0';
        neo_program_add_string(allocator, ctx->program, s + 1);
      }
      neo_allocator_free(allocator, s);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_CONCAT);
      qit = neo_list_node_next(qit);
    }
  }
}

static neo_variable_t neo_token_serialize(neo_allocator_t allocator,
                                          neo_token_t token) {
  size_t len = token->location.end.offset - token->location.begin.offset;
  wchar_t *buf =
      neo_allocator_alloc(allocator, len * 2 * sizeof(wchar_t), NULL);
  wchar_t *dst = buf;
  const char *src = token->location.end.offset;
  while (src != token->location.end.offset) {
    if (*src == '\"') {
      *dst++ = '\\';
      *dst++ = '\"';
    } else if (*src == '\n') {
      *dst++ = '\\';
      *dst++ = 'n';
    } else if (*src == '\r') {
      *dst++ = '\\';
      *dst++ = 'r';
    } else {
      *dst++ = *src++;
    }
  }
  *dst = 0;
  neo_variable_t variable = neo_create_variable_string(allocator, buf);
  neo_allocator_free(allocator, buf);
  return variable;
}

static neo_variable_t
neo_serialize_ast_literal_template(neo_allocator_t allocator,
                                   neo_ast_literal_template_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_LITERAL_TEMPLATE"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"tag",
                   neo_ast_node_serialize(allocator, node->tag));
  neo_variable_set(variable, L"expressions",
                   neo_ast_node_list_serialize(allocator, node->expressions));
  neo_variable_set(
      variable, L"quasis",
      neo_create_variable_array(allocator, node->quasis,
                                (neo_serialize_fn_t)neo_token_serialize));
  return variable;
}

static neo_ast_literal_template_t
neo_create_ast_literal_template(neo_allocator_t allocator) {
  neo_ast_literal_template_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_literal_template_t),
                          neo_ast_literal_template_dispose);
  neo_list_initialize_t initialize = {true};
  node->expressions = neo_create_list(allocator, &initialize);
  node->quasis = neo_create_list(allocator, &initialize);
  node->tag = NULL;
  node->node.type = NEO_NODE_TYPE_LITERAL_TEMPLATE;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_literal_template;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_literal_template_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_literal_template_write;
  return node;
}

neo_ast_node_t neo_ast_read_literal_template(neo_allocator_t allocator,
                                             const wchar_t *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  if (*current.offset != '`') {
    return NULL;
  }
  neo_ast_literal_template_t node = neo_create_ast_literal_template(allocator);
  token = TRY(neo_read_template_string_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    goto onerror;
  }
  if (token->type == NEO_TOKEN_TYPE_TEMPLATE_STRING) {
    neo_list_push(node->quasis, token);
  } else {
    neo_list_push(node->quasis, token);
    SKIP_ALL(allocator, file, &current, onerror);
    for (;;) {
      neo_ast_node_t expr =
          TRY(neo_ast_read_expression(allocator, file, &current)) {
        goto onerror;
      };
      if (!expr) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->expressions, expr);
      SKIP_ALL(allocator, file, &current, onerror);
      token = TRY(neo_read_template_string_token(allocator, file, &current)) {
        goto onerror;
      };
      if (!token) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              current.line, current.column);
        goto onerror;
      }
      if (token->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_END) {
        neo_list_push(node->quasis, token);
        break;
      }
      neo_list_push(node->quasis, token);
      SKIP_ALL(allocator, file, &current, onerror);
    }
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