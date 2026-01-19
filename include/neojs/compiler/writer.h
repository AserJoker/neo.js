#ifndef _H_NEO_COMPILER_WRITER_
#define _H_NEO_COMPILER_WRITER_
#include "neojs/core/allocator.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "neojs/compiler/program.h"
#include "neojs/compiler/scope.h"
typedef struct _neo_write_scope_t *neo_write_scope_t;

typedef struct _neo_ast_node_t *neo_ast_node_t;

typedef struct _neo_write_context_t {
  neo_js_program_t program;
  neo_write_scope_t scope;
  char *label;
  bool is_async;
  bool is_generator;
  bool is_loop;
} *neo_write_context_t;

typedef void (*neo_write_fn_t)(neo_allocator_t allocator,
                               neo_write_context_t ctx, neo_ast_node_t node);

neo_write_context_t neo_create_write_context(neo_allocator_t allocator,
                                             neo_js_program_t program);

void neo_writer_push_scope(neo_allocator_t allocator, neo_write_context_t ctx,
                           neo_compile_scope_t scope);

void neo_writer_pop_scope(neo_allocator_t allocator, neo_write_context_t ctx,
                          neo_compile_scope_t scope);

void neo_write_optional_chain(neo_allocator_t allocator,
                              neo_write_context_t ctx, neo_ast_node_t node,
                              neo_list_t addresses);

#ifdef __cplusplus
}
#endif
#endif