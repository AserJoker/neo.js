#include "compiler/ast/declaration_export.h"
#include "compiler/asm.h"
#include "compiler/ast/declaration_class.h"
#include "compiler/ast/declaration_function.h"
#include "compiler/ast/declaration_variable.h"
#include "compiler/ast/export_all.h"
#include "compiler/ast/export_default.h"
#include "compiler/ast/export_namespace.h"
#include "compiler/ast/export_specifier.h"
#include "compiler/ast/expression_class.h"
#include "compiler/ast/expression_function.h"
#include "compiler/ast/import_attribute.h"
#include "compiler/ast/literal_string.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_array_item.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/ast/pattern_object_item.h"
#include "compiler/ast/pattern_rest.h"
#include "compiler/ast/variable_declarator.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/list.h"
#include "core/location.h"
#include <string.h>

static void
neo_ast_declaration_export_dispose(neo_allocator_t allocator,
                                   neo_ast_declaration_export_t node) {
  neo_allocator_free(allocator, node->attributes);
  neo_allocator_free(allocator, node->source);
  neo_allocator_free(allocator, node->specifiers);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_declaration_export_variable(neo_allocator_t allocator,
                                                neo_write_context_t ctx,
                                                neo_ast_node_t node) {
  if (node->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, node->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
    neo_program_add_string(allocator, ctx->program, name);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_EXPORT);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_REST) {
    neo_ast_pattern_rest_t rest = (neo_ast_pattern_rest_t)node;
    neo_ast_declaration_export_variable(allocator, ctx, rest->identifier);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_ARRAY) {
    neo_ast_pattern_array_t array = (neo_ast_pattern_array_t)node;
    for (neo_list_node_t it = neo_list_get_first(array->items);
         it != neo_list_get_tail(array->items); it = neo_list_node_next(it)) {
      neo_ast_node_t item = neo_list_node_get(it);
      if (item) {
        neo_ast_declaration_export_variable(allocator, ctx, item);
      }
    }
  } else if (node->type == NEO_NODE_TYPE_PATTERN_OBJECT) {
    neo_ast_pattern_object_t object = (neo_ast_pattern_object_t)node;
    for (neo_list_node_t it = neo_list_get_first(object->items);
         it != neo_list_get_tail(object->items); it = neo_list_node_next(it)) {
      neo_ast_node_t item = neo_list_node_get(it);
      neo_ast_declaration_export_variable(allocator, ctx, item);
    }
  } else if (node->type == NEO_NODE_TYPE_PATTERN_ARRAY_ITEM) {
    neo_ast_pattern_array_item_t item = (neo_ast_pattern_array_item_t)node;
    neo_ast_declaration_export_variable(allocator, ctx, item->identifier);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_OBJECT_ITEM) {
    neo_ast_pattern_object_item_t item = (neo_ast_pattern_object_item_t)node;
    if (item->alias) {
      neo_ast_declaration_export_variable(allocator, ctx, item->alias);
    } else {
      neo_ast_declaration_export_variable(allocator, ctx, item->identifier);
    }
  }
}

static void
neo_ast_declaration_export_write(neo_allocator_t allocator,
                                 neo_write_context_t ctx,
                                 neo_ast_declaration_export_t self) {
  if (self->source) {
    char *name = neo_location_get(allocator, self->source->location);
    name[strlen(name) - 1] = 0;
    neo_program_add_code(allocator, ctx->program, NEO_ASM_IMPORT);
    neo_program_add_string(allocator, ctx->program, name + 1);
    for (neo_list_node_t it = neo_list_get_first(self->attributes);
         it != neo_list_get_tail(self->attributes);
         it = neo_list_node_next(it)) {
      neo_ast_node_t attr = neo_list_node_get(it);
      TRY(attr->write(allocator, ctx, attr)) { return; }
      neo_program_add_string(allocator, ctx->program, name + 1);
    }
    neo_allocator_free(allocator, name);
    for (neo_list_node_t it = neo_list_get_first(self->specifiers);
         it != neo_list_get_tail(self->specifiers);
         it = neo_list_node_next(it)) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 1);
      neo_ast_node_t spec = neo_list_node_get(it);
      if (spec->type == NEO_NODE_TYPE_EXPORT_NAMESPACE) {
        neo_ast_export_namespace_t exp = (neo_ast_export_namespace_t)spec;
        char *name = neo_location_get(allocator, exp->identifier->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_EXPORT);
        if (exp->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
          neo_program_add_string(allocator, ctx->program, name);
        } else {
          name[strlen(name) - 1] = 0;
          neo_program_add_string(allocator, ctx->program, name + 1);
        }
        neo_allocator_free(allocator, name);
      } else if (spec->type == NEO_NODE_TYPE_EXPORT_ALL) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_EXPORT_ALL);
      } else if (spec->type == NEO_NODE_TYPE_EXPORT_SPECIFIER) {
        neo_ast_export_specifier_t exp = (neo_ast_export_specifier_t)spec;
        char *name = neo_location_get(allocator, exp->identifier->location);
        if (exp->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
          neo_program_add_string(allocator, ctx->program, name);
        } else {
          name[strlen(name) - 1] = 0;
          neo_program_add_string(allocator, ctx->program, name + 1);
        }
        neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
        neo_ast_node_t identifier = exp->alias;
        if (!identifier) {
          identifier = exp->identifier;
        }
        neo_program_add_code(allocator, ctx->program, NEO_ASM_EXPORT);
        if (identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
          neo_program_add_string(allocator, ctx->program, name);
        } else {
          name[strlen(name) - 1] = 0;
          neo_program_add_string(allocator, ctx->program, name + 1);
        }
        neo_allocator_free(allocator, name);
      }
    }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    return;
  } else {
    for (neo_list_node_t it = neo_list_get_first(self->specifiers);
         it != neo_list_get_tail(self->specifiers);
         it = neo_list_node_next(it)) {
      neo_ast_node_t item = neo_list_node_get(it);
      if (item->type == NEO_NODE_TYPE_DECLARATION_FUNCTION) {
        neo_ast_declaration_function_t declar =
            (neo_ast_declaration_function_t)item;
        neo_ast_expression_function_t func =
            (neo_ast_expression_function_t)declar->declaration;
        TRY(item->write(allocator, ctx, item)) { return; }
        char *name = neo_location_get(allocator, func->name->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
        neo_program_add_string(allocator, ctx->program, name);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_EXPORT);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else if (item->type == NEO_NODE_TYPE_DECLARATION_CLASS) {
        neo_ast_declaration_class_t declar = (neo_ast_declaration_class_t)item;
        neo_ast_expression_class_t clazz =
            (neo_ast_expression_class_t)declar->declaration;
        TRY(item->write(allocator, ctx, item)) { return; }
        char *name = neo_location_get(allocator, clazz->name->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
        neo_program_add_string(allocator, ctx->program, name);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_EXPORT);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else if (item->type == NEO_NODE_TYPE_DECLARATION_VARIABLE) {
        neo_ast_declaration_variable_t declar =
            (neo_ast_declaration_variable_t)item;
        TRY(item->write(allocator, ctx, item)) { return; }
        for (neo_list_node_t it = neo_list_get_first(declar->declarators);
             it != neo_list_get_tail(declar->declarators);
             it = neo_list_node_next(it)) {
          neo_ast_variable_declarator_t declarator = neo_list_node_get(it);
          neo_ast_declaration_export_variable(allocator, ctx,
                                              declarator->identifier);
        }
      } else {
        TRY(item->write(allocator, ctx, item)) { return; }
      }
    }
  }
}
static void
neo_ast_declaration_export_resolve_closure(neo_allocator_t allocator,
                                           neo_ast_declaration_export_t self,
                                           neo_list_t closure) {
  if (!self->source) {
    for (neo_list_node_t it = neo_list_get_first(self->specifiers);
         it != neo_list_get_tail(self->specifiers);
         it = neo_list_node_next(it)) {
      neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
      item->resolve_closure(allocator, item, closure);
    }
  }
}

static neo_any_t
neo_serialize_ast_declaration_export(neo_allocator_t allocator,
                                     neo_ast_declaration_export_t node) {
  neo_any_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_variable_string(allocator,
                                         "NEO_NODE_TYPE_DECLARATION_EXPORT"));
  neo_any_set(variable, "source",
              neo_ast_node_serialize(allocator, node->source));
  neo_any_set(variable, "attributes",
              neo_ast_node_list_serialize(allocator, node->attributes));
  neo_any_set(variable, "specifiers",
              neo_ast_node_list_serialize(allocator, node->specifiers));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_declaration_export_t
neo_create_ast_declaration_export(neo_allocator_t allocator) {
  neo_ast_declaration_export_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_declaration_export_t),
      neo_ast_declaration_export_dispose);
  node->node.type = NEO_NODE_TYPE_DECLARATION_EXPORT;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_declaration_export;
  node->node.write = (neo_write_fn_t)neo_ast_declaration_export_write;
  node->source = NULL;
  neo_list_initialize_t initialize = {true};
  node->attributes = neo_create_list(allocator, &initialize);
  node->specifiers = neo_create_list(allocator, &initialize);
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_declaration_export_resolve_closure;
  return node;
}

neo_ast_node_t neo_ast_read_declaration_export(neo_allocator_t allocator,
                                               const char *file,
                                               neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_declaration_export_t node =
      neo_create_ast_declaration_export(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "export")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  neo_ast_node_t specifier =
      TRY(neo_ast_read_declaration_variable(allocator, file, &current)) {
    goto onerror;
  }
  if (!specifier) {
    specifier =
        TRY(neo_ast_read_declaration_function(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!specifier) {
    specifier = TRY(neo_ast_read_declaration_class(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!specifier) {
    specifier = TRY(neo_ast_read_export_default(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (specifier) {
    neo_list_push(node->specifiers, specifier);
    goto onfinish;
  }
  if (!specifier) {
    specifier = TRY(neo_ast_read_export_namespace(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!specifier) {
    specifier = TRY(neo_ast_read_export_all(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!specifier) {
    if (*current.offset != '{') {
      THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset != '}') {
      for (;;) {
        specifier =
            TRY(neo_ast_read_export_specifier(allocator, file, &current)) {
          goto onerror;
        }
        if (!specifier) {
          THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
                current.line, current.column);
          goto onerror;
        }
        neo_list_push(node->specifiers, specifier);
        SKIP_ALL(allocator, file, &current, onerror);
        if (*current.offset == ',') {
          current.offset++;
          current.column++;
          SKIP_ALL(allocator, file, &current, onerror);
        } else if (*current.offset == '}') {
          break;
        } else {
          THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
                current.line, current.column);
          goto onerror;
        }
      }
    }
    current.offset++;
    current.column++;
  } else {
    neo_list_push(node->specifiers, specifier);
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  token = neo_read_identify_token(allocator, file, &cur);
  if (!token || !neo_location_is(token->location, "from")) {
    if (specifier && (specifier->type == NEO_NODE_TYPE_EXPORT_NAMESPACE ||
                      specifier->type == NEO_NODE_TYPE_EXPORT_ALL)) {
      THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            cur.line, cur.column);
      goto onerror;
    }
    neo_allocator_free(allocator, token);
  } else {
    neo_allocator_free(allocator, token);
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
    node->source = TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    }
    cur = current;
    SKIP_ALL(allocator, file, &cur, onerror);
    token = neo_read_identify_token(allocator, file, &cur);
    if (token && neo_location_is(token->location, "assert")) {
      current = cur;
      neo_allocator_free(allocator, token);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset != '{') {
        THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
              current.line, current.column);
        goto onerror;
      }
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset != '}') {
        for (;;) {
          neo_ast_node_t attribute =
              TRY(neo_ast_read_import_attribute(allocator, file, &current)) {
            goto onerror;
          }
          if (!attribute) {
            THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
                  file, current.line, current.column);
            goto onerror;
          }
          neo_list_push(node->attributes, attribute);
          SKIP_ALL(allocator, file, &current, onerror);
          if (*current.offset == '}') {
            break;
          } else if (*current.offset == ',') {
            current.offset++;
            current.column++;
            SKIP_ALL(allocator, file, &current, onerror);
          } else {
            THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
                  file, current.line, current.column);
            goto onerror;
          }
        }
        if (*current.offset != '}') {
          THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
                current.line, current.column);
          goto onerror;
        }
        current.offset++;
        current.column++;
      }
    } else {
      neo_allocator_free(allocator, token);
    }
  }
onfinish: {
  uint32_t line = current.line;
  cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (cur.line == line) {
    if (*cur.offset && *cur.offset != ';') {
      THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            cur.line, cur.column);
      goto onerror;
    }
  }
}
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}