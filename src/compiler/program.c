#include "compiler/program.h"
#include "compiler/asm.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/list.h"
#include "core/string.h"
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
  program->file = neo_allocator_alloc(allocator, strlen(file) + 1, NULL);
  strcpy(program->file, file);
  program->codes = neo_create_buffer(allocator, 128);
  neo_list_initialize_t initialize = {true};
  program->constants = neo_create_list(allocator, &initialize);
  return program;
}

size_t neo_program_add_constant(neo_allocator_t allocator, neo_program_t self,
                                const char *constant) {
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(self->constants);
       it != neo_list_get_tail(self->constants); it = neo_list_node_next(it)) {
    if (strcmp(neo_list_node_get(it), constant) == 0) {
      return idx;
    }
    idx++;
  }
  size_t len = strlen(constant);
  char *buf = neo_allocator_alloc(allocator, len + 1, NULL);
  strcpy(buf, constant);
  buf[len] = 0;
  neo_list_push(self->constants, buf);
  return neo_list_get_size(self->constants) - 1;
}

void neo_program_add_code(neo_allocator_t allocator, neo_program_t self,
                          uint16_t code) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &code,
                   sizeof(code));
}
void neo_program_add_address(neo_allocator_t allocator, neo_program_t self,
                             size_t code) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &code,
                   sizeof(code));
}

void neo_program_add_string(neo_allocator_t allocator, neo_program_t self,
                            const char *string) {
  size_t idx = neo_program_add_constant(allocator, self, string);
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &idx,
                   sizeof(idx));
}

void neo_program_add_number(neo_allocator_t allocator, neo_program_t self,
                            double number) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &number,
                   sizeof(number));
}
void neo_program_add_integer(neo_allocator_t allocator, neo_program_t self,
                             int32_t number) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &number,
                   sizeof(number));
}

void neo_program_add_boolean(neo_allocator_t allocator, neo_program_t self,
                             bool boolean) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &boolean,
                   sizeof(boolean));
}
void neo_program_set_current(neo_program_t self, size_t address) {
  *(size_t *)((char *)neo_buffer_get(self->codes) + address) =
      neo_buffer_get_size(self->codes);
}

static const char *neo_program_get_string(neo_program_t program,
                                          size_t *offset) {
  size_t idx = *(size_t *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(size_t);
  neo_list_node_t it = neo_list_get_first(program->constants);
  for (size_t i = 0; i < idx; i++) {
    it = neo_list_node_next(it);
  }
  return (const char *)neo_list_node_get(it);
}
static const bool neo_program_get_bool(neo_program_t program, size_t *offset) {
  bool value = *(bool *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(bool);
  return value;
}
static const double neo_program_get_number(neo_program_t program,
                                           size_t *offset) {
  double value = *(double *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(double);
  return value;
}
static const size_t neo_program_get_address(neo_program_t program,
                                            size_t *offset) {
  size_t value = *(size_t *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(size_t);
  return value;
}
static const int32_t neo_program_get_integer(neo_program_t program,
                                             size_t *offset) {
  int32_t value =
      *(int32_t *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(int32_t);
  return value;
}

void neo_program_write(neo_allocator_t allocator, FILE *fp,
                       neo_program_t self) {
  fprintf(fp, "[section .metadata]\n");
  fprintf(fp, ".file: %s\n", self->file);
  size_t idx = 0;
  fprintf(fp, "[section .constants]\n");
  for (neo_list_node_t it = neo_list_get_first(self->constants);
       it != neo_list_get_tail(self->constants); it = neo_list_node_next(it)) {
    char *constant = neo_string_encode(allocator, neo_list_node_get(it));
    fprintf(fp, ".%ld: \"%s\"\n", idx, constant);
    neo_allocator_free(allocator, constant);
    idx++;
  }
  fprintf(fp, "[section .data]\n");
  size_t offset = 0;
  while (offset < neo_buffer_get_size(self->codes)) {
    fprintf(fp, "%ld: ", offset);
    neo_asm_code_t code =
        (neo_asm_code_t) *
        (uint16_t *)((char *)neo_buffer_get(self->codes) + offset);
    offset += 2;
    switch (code) {
    case NEO_ASM_PUSH_SCOPE:
      fprintf(fp, "NEO_ASM_PUSH_SCOPE\n");
      break;
    case NEO_ASM_POP_SCOPE:
      fprintf(fp, "NEO_ASM_POP_SCOPE\n");
      break;
    case NEO_ASM_POP:
      fprintf(fp, "NEO_ASM_POP\n");
      break;
    case NEO_ASM_STORE: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_STORE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_DEF: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DEF \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_LOAD: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_LOAD \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_CLONE: {
      fprintf(fp, "NEO_ASM_CLONE\n");
    } break;
    case NEO_ASM_WITH:
      fprintf(fp, "NEO_ASM_WITH\n");
      break;
    case NEO_ASM_INIT_FIELD:
      fprintf(fp, "NEO_ASM_INIT_FIELD\n");
      break;
    case NEO_ASM_INIT_ACCESSOR:
      fprintf(fp, "NEO_ASM_INIT_ACCESSOR\n");
      break;
    case NEO_ASM_PUSH_UNDEFINED:
      fprintf(fp, "NEO_ASM_PUSH_UNDEFINED\n");
      break;
    case NEO_ASM_PUSH_NULL:
      fprintf(fp, "NEO_ASM_PUSH_NULL\n");
      break;
    case NEO_ASM_PUSH_NAN:
      fprintf(fp, "NEO_ASM_PUSH_NAN\n");
      break;
    case NEO_ASM_PUSH_INFINTY:
      fprintf(fp, "NEO_ASM_PUSH_INFINTY\n");
      break;
    case NEO_ASM_PUSH_UNINITIALIZED:
      fprintf(fp, "NEO_ASM_PUSH_UNINITIALIZED\n");
      break;
    case NEO_ASM_PUSH_TRUE:
      fprintf(fp, "NEO_ASM_PUSH_TRUE\n");
      break;
    case NEO_ASM_PUSH_FALSE:
      fprintf(fp, "NEO_ASM_PUSH_FALSE\n");
      break;
    case NEO_ASM_PUSH_NUMBER: {
      fprintf(fp, "NEO_ASM_PUSH_NUMBER %lg\n",
              neo_program_get_number(self, &offset));
    } break;
    case NEO_ASM_PUSH_STRING: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_STRING \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_BIGINT: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_BIGINT \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_REGEX: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_REGEX \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_FUNCTION:
      fprintf(fp, "NEO_ASM_PUSH_FUNCTION\n");
      break;
    case NEO_ASM_PUSH_ASYNC_FUNCTION:
      fprintf(fp, "NEO_ASM_PUSH_ASYNC_FUNCTION\n");
      break;
    case NEO_ASM_PUSH_LAMBDA:
      fprintf(fp, "NEO_ASM_PUSH_LAMBDA\n");
      break;
    case NEO_ASM_PUSH_ASYNC_LAMBDA:
      fprintf(fp, "NEO_ASM_PUSH_ASYNC_LAMBDA\n");
      break;
    case NEO_ASM_PUSH_GENERATOR:
      fprintf(fp, "NEO_ASM_PUSH_GENERATOR\n");
      break;
    case NEO_ASM_PUSH_ASYNC_GENERATOR:
      fprintf(fp, "NEO_ASM_PUSH_ASYNC_GENERATOR\n");
      break;
    case NEO_ASM_PUSH_OBJECT:
      fprintf(fp, "NEO_ASM_PUSH_OBJECT\n");
      break;
    case NEO_ASM_PUSH_ARRAY:
      fprintf(fp, "NEO_ASM_PUSH_ARRAY %lg\n",
              neo_program_get_number(self, &offset));
      break;
    case NEO_ASM_PUSH_THIS:
      fprintf(fp, "NEO_ASM_PUSH_THIS\n");
      break;
    case NEO_ASM_PUSH_SUPER:
      fprintf(fp, "NEO_ASM_PUSH_SUPER\n");
      break;
    case NEO_ASM_PUSH_VALUE:
      fprintf(fp, "NEO_ASM_PUSH_VALUE %d\n",
              neo_program_get_integer(self, &offset));
      break;
    case NEO_ASM_SET_NAME:
      fprintf(fp, "NEO_ASM_SET_NAME\n");
      break;
    case NEO_ASM_SET_SOURCE: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_SOURCE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SET_ADDRESS:
      fprintf(fp, "NEO_ASM_SET_ADDRESS %ld\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_SET_CLOSURE: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_CLOSURE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_EXTEND:
      fprintf(fp, "NEO_ASM_EXTEND\n");
      break;
    case NEO_ASM_DECORATOR:
      fprintf(fp, "NEO_ASM_DECORATOR\n");
      break;
    case NEO_ASM_SET_CONST:
      fprintf(fp, "NEO_ASM_SET_CONST\n");
      break;
    case NEO_ASM_SET_USING:
      fprintf(fp, "NEO_ASM_SET_USING\n");
      break;
    case NEO_ASM_DIRECTIVE: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DIRECTIVE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_CALL:
      fprintf(fp, "NEO_ASM_CALL\n");
      break;
    case NEO_ASM_MEMBER_CALL:
      fprintf(fp, "NEO_ASM_MEMBER_CALL\n");
      break;
    case NEO_ASM_GET_FIELD:
      fprintf(fp, "NEO_ASM_GET_FIELD\n");
      break;
    case NEO_ASM_SET_FIELD:
      fprintf(fp, "NEO_ASM_SET_FIELD\n");
      break;
    case NEO_ASM_SET_GETTER:
      fprintf(fp, "NEO_ASM_SET_GETTER\n");
      break;
    case NEO_ASM_SET_SETTER:
      fprintf(fp, "NEO_ASM_SET_SETTER\n");
      break;
    case NEO_ASM_SET_METHOD:
      fprintf(fp, "NEO_ASM_SET_METHOD\n");
      break;
    case NEO_ASM_JNULL:
      fprintf(fp, "NEO_ASM_JNULL %ld\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_JNOT_NULL:
      fprintf(fp, "NEO_ASM_JNOT_NULL %ld\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_JFALSE:
      fprintf(fp, "NEO_ASM_JFALSE %ld\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_JTRUE:
      fprintf(fp, "NEO_ASM_JTRUE %ld\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_JMP:
      fprintf(fp, "NEO_ASM_JMP %ld\n", neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_BREAK: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_BREAK \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_CONTINUE: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_CONTINUE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_THROW:
      fprintf(fp, "NEO_ASM_THROW\n");
      break;
    case NEO_ASM_TRY_BEGIN: {
      size_t onerror = neo_program_get_address(self, &offset);
      size_t onfinish = neo_program_get_address(self, &offset);
      fprintf(fp, "NEO_ASM_TRY_BEGIN %ld,%ld\n", onerror, onfinish);
      break;
    }
    case NEO_ASM_TRY_END:
      fprintf(fp, "NEO_ASM_TRY_END\n");
      break;
    case NEO_ASM_RET:
      fprintf(fp, "NEO_ASM_RET\n");
      break;
    case NEO_ASM_HLT:
      fprintf(fp, "NEO_ASM_HLT\n");
      break;
    case NEO_ASM_AWAIT:
      fprintf(fp, "NEO_ASM_AWAIT\n");
      break;
    case NEO_ASM_YIELD:
      fprintf(fp, "NEO_ASM_YIELD\n");
      break;
    case NEO_ASM_NEXT:
      fprintf(fp, "NEO_ASM_NEXT\n");
      break;
    case NEO_ASM_KEYS:
      fprintf(fp, "NEO_ASM_KEYS\n");
      break;
    case NEO_ASM_ITERATOR:
      fprintf(fp, "NEO_ASM_ITERATOR\n");
      break;
    case NEO_ASM_REST:
      fprintf(fp, "NEO_ASM_REST\n");
      break;
    case NEO_ASM_REST_OBJECT:
      fprintf(fp, "NEO_ASM_REST_OBJECT\n");
      break;
    case NEO_ASM_PUSH_BREAK_LABEL: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_BREAK_LABEL \"%s\",%ld\n", constant,
              neo_program_get_address(self, &offset));
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_CONTINUE_LABEL: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_CONTINUE_LABEL \"%s\",%ld\n", constant,
              neo_program_get_address(self, &offset));
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_POP_LABEL:
      fprintf(fp, "NEO_ASM_POP_LABEL\n");
      break;
    case NEO_ASM_IMPORT: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_IMPORT \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_EXPORT: {
      char *constant =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_EXPORT \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_EXPORT_ALL:
      fprintf(fp, "NEO_ASM_EXPORT_ALL\n");
      break;
    case NEO_ASM_ASSERT: {
      char *type =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      char *value =
          neo_string_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_ASSERT \"%s\",\"%s\"\n", type, value);
      neo_allocator_free(allocator, value);
      neo_allocator_free(allocator, type);
    } break;
    case NEO_ASM_EQ:
      fprintf(fp, "NEO_ASM_EQ\n");
      break;
    case NEO_ASM_NE:
      fprintf(fp, "NEO_ASM_NE\n");
      break;
    case NEO_ASM_SNE:
      fprintf(fp, "NEO_ASM_SNE\n");
      break;
    case NEO_ASM_SEQ:
      fprintf(fp, "NEO_ASM_SEQ\n");
      break;
    case NEO_ASM_BREAKPOINT:
      fprintf(fp, "NEO_ASM_BREAKPOINT\n");
      break;
    case NEO_ASM_NEW:
      fprintf(fp, "NEO_ASM_NEW\n");
      break;
    case NEO_ASM_DEL:
      fprintf(fp, "NEO_ASM_DEL\n");
      break;
    case NEO_ASM_TYPEOF:
      fprintf(fp, "NEO_ASM_TYPEOF\n");
      break;
    case NEO_ASM_VOID:
      fprintf(fp, "NEO_ASM_VOID\n");
      break;
    case NEO_ASM_CONCAT:
      fprintf(fp, "NEO_ASM_CONCAT\n");
      break;
    case NEO_ASM_SPREAD:
      fprintf(fp, "NEO_ASM_SPREAD\n");
      break;
    case NEO_ASM_GT:
      fprintf(fp, "NEO_ASM_GT\n");
      break;
    case NEO_ASM_LT:
      fprintf(fp, "NEO_ASM_LT\n");
      break;
    case NEO_ASM_GE:
      fprintf(fp, "NEO_ASM_GE\n");
      break;
    case NEO_ASM_LE:
      fprintf(fp, "NEO_ASM_LE\n");
      break;
    case NEO_ASM_INC:
      fprintf(fp, "NEO_ASM_INC\n");
      break;
    case NEO_ASM_DEC:
      fprintf(fp, "NEO_ASM_DEC\n");
      break;
    case NEO_ASM_ADD:
      fprintf(fp, "NEO_ASM_ADD\n");
      break;
    case NEO_ASM_SUB:
      fprintf(fp, "NEO_ASM_SUB\n");
      break;
    case NEO_ASM_MUL:
      fprintf(fp, "NEO_ASM_MUL\n");
      break;
    case NEO_ASM_DIV:
      fprintf(fp, "NEO_ASM_DIV\n");
      break;
    case NEO_ASM_MOD:
      fprintf(fp, "NEO_ASM_MOD\n");
      break;
    case NEO_ASM_POW:
      fprintf(fp, "NEO_ASM_POW\n");
      break;
    case NEO_ASM_NOT:
      fprintf(fp, "NEO_ASM_NOT\n");
      break;
    case NEO_ASM_AND:
      fprintf(fp, "NEO_ASM_AND\n");
      break;
    case NEO_ASM_OR:
      fprintf(fp, "NEO_ASM_OR\n");
      break;
    case NEO_ASM_XOR:
      fprintf(fp, "NEO_ASM_XOR\n");
      break;
    case NEO_ASM_SHR:
      fprintf(fp, "NEO_ASM_SHR\n");
      break;
    case NEO_ASM_SHL:
      fprintf(fp, "NEO_ASM_SHL\n");
      break;
    case NEO_ASM_USHR:
      fprintf(fp, "NEO_ASM_USHR\n");
      break;
    case NEO_ASM_PLUS:
      fprintf(fp, "NEO_ASM_PLUS\n");
      break;
    case NEO_ASM_NEG:
      fprintf(fp, "NEO_ASM_NEG\n");
      break;
    case NEO_ASM_LOGICAL_NOT:
      fprintf(fp, "NEO_ASM_LOGICAL_NOT\n");
      break;
    }
  }
}