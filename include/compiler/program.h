#pragma once
#include "core/error.h"
#include "core/list.h"
#include <stdint.h>
typedef struct _neo_program_t *neo_program_t;

struct _neo_program_t {
  const char *file;
  uint16_t *codes;
  size_t size;
  size_t capacity;
  neo_allocator_t allocator;
  neo_list_t constants;
};

neo_program_t neo_program_create(neo_allocator_t allocator);

neo_error_t neo_program_add_constant(neo_program_t self, const char *constant,
                                     size_t *index);

neo_error_t neo_program_add_code(neo_program_t self, uint16_t code,
                                 uint16_t arg);

neo_error_t neo_program_add_string(neo_program_t self, const char *string);

neo_error_t neo_program_add_integer(neo_program_t self, double number);

neo_error_t neo_program_add_boolean(neo_program_t self, bool boolean);