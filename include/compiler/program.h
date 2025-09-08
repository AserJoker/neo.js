#ifndef _H_NEO_COMPILER_PROGRAM_
#define _H_NEO_COMPILER_PROGRAM_
#ifdef __cplusplus
extern "C" {
#endif
#include "core/buffer.h"
#include "core/list.h"
#include <stdint.h>
#include <stdio.h>
typedef struct _neo_program_t *neo_program_t;

struct _neo_program_t {
  char *filename;
  char *dirname;
  neo_buffer_t codes;
  neo_list_t constants;
};

neo_program_t neo_create_program(neo_allocator_t allocator, const char *file);

size_t neo_program_add_constant(neo_allocator_t allocator, neo_program_t self,
                                const char *constant);

void neo_program_add_code(neo_allocator_t allocator, neo_program_t self,
                          uint16_t code);

void neo_program_add_address(neo_allocator_t allocator, neo_program_t self,
                             size_t code);

void neo_program_add_string(neo_allocator_t allocator, neo_program_t self,
                            const char *string);

void neo_program_add_number(neo_allocator_t allocator, neo_program_t self,
                            double number);

void neo_program_add_integer(neo_allocator_t allocator, neo_program_t self,
                             int32_t number);

void neo_program_add_boolean(neo_allocator_t allocator, neo_program_t self,
                             bool boolean);

void neo_program_set_current(neo_program_t self, size_t address);

void neo_program_write(neo_allocator_t allocator, FILE *fp, neo_program_t self);

#ifdef __cplusplus
}
#endif
#endif