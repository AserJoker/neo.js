#include "compiler/program.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/list.h"
#include <string.h>
static void neo_program_dispose(neo_allocator_t allocator,
                                neo_program_t program) {
  neo_allocator_free(allocator, program->constants);
  neo_allocator_free(allocator, program->codes);
  neo_allocator_free(allocator, program->file);
}

neo_program_t neo_create_program(neo_allocator_t allocator, const char *file) {
  neo_program_t program = (neo_program_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_program_t), neo_program_dispose);
  program->allocator = allocator;
  program->file = neo_allocator_alloc(allocator, strlen(file) + 1, NULL);
  strcpy(program->file, file);
  program->codes = neo_create_buffer(allocator, 128);
  neo_list_initialize_t initialize = {true};
  program->constants = neo_create_list(allocator, &initialize);
  return program;
}

size_t neo_program_add_constant(neo_program_t self, const char *constant) {
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(self->constants);
       it != neo_list_get_tail(self->constants); it = neo_list_node_next(it)) {
    if (strcmp(neo_list_node_get(it), constant) == 0) {
      return idx;
    }
    idx++;
  }
  size_t len = strlen(constant);
  char *buf = neo_allocator_alloc(self->allocator, len + 1, NULL);
  strcpy(buf, constant);
  buf[len] = 0;
  neo_list_push(self->constants, buf);
  return neo_list_get_size(self->constants) - 1;
}

void neo_program_add_code(neo_program_t self, uint16_t code) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &code,
                   sizeof(code));
}

void neo_program_add_string(neo_program_t self, const char *string) {
  size_t idx = neo_program_add_constant(self, string);
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &idx,
                   sizeof(idx));
}

void neo_program_add_integer(neo_program_t self, double number) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &number,
                   sizeof(number));
}

void neo_program_add_boolean(neo_program_t self, bool boolean) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &boolean,
                   sizeof(boolean));
}
