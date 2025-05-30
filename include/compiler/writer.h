#pragma once
#include "compiler/ast/node.h"
#include "compiler/program.h"

typedef struct _neo_writer_t *neo_writer_t;

neo_writer_t neo_writer_create(neo_allocator_t allocator);

neo_program_t neo_writer_write(neo_writer_t self, const char *file,
                               neo_ast_node_t root);