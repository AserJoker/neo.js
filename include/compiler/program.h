#pragma once
#include "core/buffer.h"
#include "core/list.h"
#include <stdint.h>
typedef struct _neo_program_t *neo_program_t;

struct _neo_program_t {
  char *file;
  neo_buffer_t codes;
  neo_allocator_t allocator;
  neo_list_t constants;
};

neo_program_t neo_create_program(neo_allocator_t allocator, const char *file);

size_t neo_program_add_constant(neo_program_t self, const char *constant);

void neo_program_add_code(neo_program_t self, uint16_t code);

void neo_program_add_string(neo_program_t self, const char *string);

void neo_program_add_integer(neo_program_t self, double number);

void neo_program_add_boolean(neo_program_t self, bool boolean);