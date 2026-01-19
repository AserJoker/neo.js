#include "neojs/compiler/program.h"
#include "neojs/compiler/asm.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/scope.h"
#include "neojs/core/allocator.h"
#include "neojs/core/buffer.h"
#include "neojs/core/list.h"
#include "neojs/core/path.h"
#include "neojs/core/string.h"
#include <string.h>

static void neo_js_program_dispose(neo_allocator_t allocator,
                                   neo_js_program_t program) {
  neo_allocator_free(allocator, program->constants);
  neo_allocator_free(allocator, program->codes);
  neo_allocator_free(allocator, program->filename);
  neo_allocator_free(allocator, program->dirname);
}

neo_js_program_t neo_create_js_program(neo_allocator_t allocator,
                                       const char *file) {
  neo_js_program_t program = (neo_js_program_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_program_t), neo_js_program_dispose);
  program->filename = neo_create_string(allocator, file);
  neo_path_t path = neo_create_path(allocator, program->filename);
  neo_path_t parent = neo_path_parent(path);
  neo_allocator_free(allocator, path);
  program->dirname = neo_path_to_string(parent);
  neo_allocator_free(allocator, parent);
  program->codes = neo_create_buffer(allocator, 128);
  neo_list_initialize_t initialize = {true};
  program->constants = neo_create_list(allocator, &initialize);
  return program;
}

size_t neo_js_program_add_constant(neo_allocator_t allocator,
                                   neo_js_program_t self,
                                   const char *constant) {
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(self->constants);
       it != neo_list_get_tail(self->constants); it = neo_list_node_next(it)) {
    if (strcmp(neo_list_node_get(it), constant) == 0) {
      return idx;
    }
    idx++;
  }
  neo_list_push(self->constants, neo_create_string(allocator, constant));
  return neo_list_get_size(self->constants) - 1;
}

void neo_js_program_add_code(neo_allocator_t allocator, neo_js_program_t self,
                             uint16_t code) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &code,
                   sizeof(code));
}
void neo_js_program_add_address(neo_allocator_t allocator,
                                neo_js_program_t self, size_t code) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &code,
                   sizeof(code));
}

void neo_js_program_add_string(neo_allocator_t allocator, neo_js_program_t self,
                               const char *string) {
  size_t idx = neo_js_program_add_constant(allocator, self, string);
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &idx,
                   sizeof(idx));
}

void neo_js_program_add_number(neo_allocator_t allocator, neo_js_program_t self,
                               double number) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &number,
                   sizeof(number));
}
void neo_js_program_add_integer(neo_allocator_t allocator,
                                neo_js_program_t self, int32_t number) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &number,
                   sizeof(number));
}

void neo_js_program_add_boolean(neo_allocator_t allocator,
                                neo_js_program_t self, bool boolean) {
  neo_buffer_write(self->codes, neo_buffer_get_size(self->codes), &boolean,
                   sizeof(boolean));
}
void neo_js_program_set_current(neo_js_program_t self, size_t address) {
  *(size_t *)((char *)neo_buffer_get(self->codes) + address) =
      neo_buffer_get_size(self->codes);
}

static const char *neo_js_program_get_string(neo_js_program_t program,
                                             size_t *offset) {
  size_t idx = *(size_t *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(size_t);
  neo_list_node_t it = neo_list_get_first(program->constants);
  for (size_t i = 0; i < idx; i++) {
    it = neo_list_node_next(it);
  }
  return (const char *)neo_list_node_get(it);
}
static const double neo_js_program_get_number(neo_js_program_t program,
                                              size_t *offset) {
  double value = *(double *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(double);
  return value;
}
static const size_t neo_js_program_get_address(neo_js_program_t program,
                                               size_t *offset) {
  size_t value = *(size_t *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(size_t);
  return value;
}
static const int32_t neo_js_program_get_integer(neo_js_program_t program,
                                                size_t *offset) {
  int32_t value =
      *(int32_t *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(int32_t);
  return value;
}

neo_ast_node_t neo_js_program_write(neo_allocator_t allocator, FILE *fp,
                                    neo_js_program_t self) {
  fprintf(fp, "[section .metadata]\n");
  fprintf(fp, ".filename: %s\n", self->filename);
  fprintf(fp, ".dirname: %s\n", self->dirname);
  size_t idx = 0;
  fprintf(fp, "[section .constants]\n");
  for (neo_list_node_t it = neo_list_get_first(self->constants);
       it != neo_list_get_tail(self->constants); it = neo_list_node_next(it)) {
    const char *current = neo_list_node_get(it);
    char *constant = neo_string_encode_escape(allocator, current);
    fprintf(fp, ".%zu: \"%s\"\n", idx, constant);
    neo_allocator_free(allocator, constant);
    idx++;
  }
  fprintf(fp, "[section .data]\n");
  size_t offset = 0;
  while (offset < neo_buffer_get_size(self->codes)) {
    fprintf(fp, "%zu: ", offset);
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
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_STORE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SAVE: {
      fprintf(fp, "NEO_ASM_SAVE\n");
    } break;
    case NEO_ASM_DEF: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DEF \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_LOAD: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_LOAD \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_INIT_FIELD:
      fprintf(fp, "NEO_ASM_INIT_FIELD\n");
      break;
    case NEO_ASM_INIT_PRIVATE_FIELD: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_INIT_PRIVATE_FIELD \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_INIT_ACCESSOR:
      fprintf(fp, "NEO_ASM_INIT_ACCESSOR\n");
      break;
    case NEO_ASM_INIT_PRIVATE_ACCESSOR: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_INIT_PRIVATE_ACCESSOR \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
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
              neo_js_program_get_number(self, &offset));
    } break;
    case NEO_ASM_PUSH_STRING: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_STRING \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_BIGINT: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_BIGINT \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_REGEXP: {
      char *content = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      char *flag = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_REGEXP \"%s\",\"%s\"\n", content, flag);
      neo_allocator_free(allocator, flag);
      neo_allocator_free(allocator, content);
    } break;
    case NEO_ASM_PUSH_FUNCTION:
      fprintf(fp, "NEO_ASM_PUSH_FUNCTION\n");
      break;
    case NEO_ASM_PUSH_CLASS:
      fprintf(fp, "NEO_ASM_PUSH_CLASS\n");
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
      fprintf(fp, "NEO_ASM_PUSH_ARRAY\n");
      break;
    case NEO_ASM_PUSH_THIS:
      fprintf(fp, "NEO_ASM_PUSH_THIS\n");
      break;
    case NEO_ASM_SUPER_CALL: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_SUPER_CALL %d,%d\n", line, column);
    } break;
    case NEO_ASM_APPEND: {
      fprintf(fp, "NEO_ASM_APPEND\n");
    } break;
    case NEO_ASM_APPEND_EMPTY: {
      fprintf(fp, "NEO_ASM_APPEND_EMPTY\n");
    } break;
    case NEO_ASM_SUPER_MEMBER_CALL: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_SUPER_MEMBER_CALL %d,%d\n", line, column);
    } break;
    case NEO_ASM_GET_SUPER_FIELD: {
      fprintf(fp, "NEO_ASM_GET_SUPER_FIELD\n");
    } break;
    case NEO_ASM_SET_SUPER_FIELD: {
      fprintf(fp, "NEO_ASM_SET_SUPER_FIELD\n");
    } break;
    case NEO_ASM_PUSH_VALUE:
      fprintf(fp, "NEO_ASM_PUSH_VALUE %d\n",
              neo_js_program_get_integer(self, &offset));
      break;
    case NEO_ASM_SET_NAME: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_NAME \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SET_SOURCE: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_SOURCE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SET_BIND:
      fprintf(fp, "NEO_ASM_SET_BIND\n");
      break;
    case NEO_ASM_SET_CLASS:
      fprintf(fp, "NEO_ASM_SET_CLASS\n");
      break;
    case NEO_ASM_SET_ADDRESS:
      fprintf(fp, "NEO_ASM_SET_ADDRESS %zu\n",
              neo_js_program_get_address(self, &offset));
      break;
    case NEO_ASM_SET_CLOSURE: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_CLOSURE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_EXTENDS:
      fprintf(fp, "NEO_ASM_EXTENDS\n");
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
    case NEO_ASM_SET_AWAIT_USING:
      fprintf(fp, "NEO_ASM_SET_AWAIT_USING\n");
      break;
    case NEO_ASM_DIRECTIVE: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DIRECTIVE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_CALL: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_CALL %d,%d\n", line, column);
    } break;
    case NEO_ASM_EVAL: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_EVAL %d,%d\n", line, column);
    } break;
    case NEO_ASM_MEMBER_CALL: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_MEMBER_CALL %d,%d\n", line, column);
    } break;
    case NEO_ASM_GET_FIELD:
      fprintf(fp, "NEO_ASM_GET_FIELD\n");
      break;
    case NEO_ASM_SET_FIELD:
      fprintf(fp, "NEO_ASM_SET_FIELD\n");
      break;
    case NEO_ASM_PRIVATE_CALL: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_PRIVATE_CALL %s,%d,%d\n", constant, line, column);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_GET_PRIVATE_FIELD: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_GET_PRIVATE_FIELD \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SET_PRIVATE_FIELD: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_PRIVATE_FIELD \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SET_GETTER:
      fprintf(fp, "NEO_ASM_SET_GETTER\n");
      break;
    case NEO_ASM_SET_SETTER:
      fprintf(fp, "NEO_ASM_SET_SETTER\n");
      break;
    case NEO_ASM_SET_METHOD:
      fprintf(fp, "NEO_ASM_SET_METHOD\n");
      break;
    case NEO_ASM_DEF_PRIVATE_GETTER: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DEF_PRIVATE_GETTER \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_DEF_PRIVATE_SETTER: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DEF_PRIVATE_SETTER \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_DEF_PRIVATE_METHOD: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DEF_PRIVATE_METHOD \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_JNULL:
      fprintf(fp, "NEO_ASM_JNULL %zu\n",
              neo_js_program_get_address(self, &offset));
      break;
    case NEO_ASM_JNOT_NULL:
      fprintf(fp, "NEO_ASM_JNOT_NULL %zu\n",
              neo_js_program_get_address(self, &offset));
      break;
    case NEO_ASM_JFALSE:
      fprintf(fp, "NEO_ASM_JFALSE %zu\n",
              neo_js_program_get_address(self, &offset));
      break;
    case NEO_ASM_JTRUE:
      fprintf(fp, "NEO_ASM_JTRUE %zu\n",
              neo_js_program_get_address(self, &offset));
      break;
    case NEO_ASM_JMP:
      fprintf(fp, "NEO_ASM_JMP %zu\n",
              neo_js_program_get_address(self, &offset));
      break;
    case NEO_ASM_BREAK: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_BREAK \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_CONTINUE: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_CONTINUE \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_THROW:
      fprintf(fp, "NEO_ASM_THROW\n");
      break;
    case NEO_ASM_TRY_BEGIN: {
      size_t onerror = neo_js_program_get_address(self, &offset);
      size_t onfinish = neo_js_program_get_address(self, &offset);
      fprintf(fp, "NEO_ASM_TRY_BEGIN %zu,%zu\n", onerror, onfinish);
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
    case NEO_ASM_RESOLVE_NEXT:
      fprintf(fp, "NEO_ASM_RESOLVE_NEXT\n");
      break;
    case NEO_ASM_KEYS:
      fprintf(fp, "NEO_ASM_KEYS\n");
      break;
    case NEO_ASM_ITERATOR:
      fprintf(fp, "NEO_ASM_ITERATOR\n");
      break;
    case NEO_ASM_ASYNC_ITERATOR:
      fprintf(fp, "NEO_ASM_ASYNC_ITERATOR\n");
      break;
    case NEO_ASM_REST:
      fprintf(fp, "NEO_ASM_REST\n");
      break;
    case NEO_ASM_REST_OBJECT:
      fprintf(fp, "NEO_ASM_REST_OBJECT\n");
      break;
    case NEO_ASM_PUSH_BREAK_LABEL: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_BREAK_LABEL \"%s\",%zu\n", constant,
              neo_js_program_get_address(self, &offset));
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_CONTINUE_LABEL: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_CONTINUE_LABEL \"%s\",%zu\n", constant,
              neo_js_program_get_address(self, &offset));
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_POP_LABEL:
      fprintf(fp, "NEO_ASM_POP_LABEL\n");
      break;
    case NEO_ASM_IMPORT: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_IMPORT \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_EXPORT: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_EXPORT \"%s\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_EXPORT_ALL:
      fprintf(fp, "NEO_ASM_EXPORT_ALL\n");
      break;
    case NEO_ASM_ASSERT: {
      char *type = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      char *value = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      char *module = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_ASSERT \"%s\",\"%s\",\"%s\"\n", type, value, module);
      neo_allocator_free(allocator, value);
      neo_allocator_free(allocator, type);
      neo_allocator_free(allocator, module);
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
    case NEO_ASM_NEW: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_NEW %d,%d\n", line, column);
    } break;
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
    case NEO_ASM_DEFER_INC:
      fprintf(fp, "NEO_ASM_DEFER_INC\n");
      break;
    case NEO_ASM_DEFER_DEC:
      fprintf(fp, "NEO_ASM_DEFER_DEC\n");
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
    case NEO_ASM_IN:
      fprintf(fp, "NEO_ASM_IN\n");
      break;
    case NEO_ASM_INSTANCE_OF:
      fprintf(fp, "NEO_ASM_INSTANCE_OF\n");
      break;
    case NEO_ASM_TAG: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_TAG %d,%d\n", line, column);
    } break;
    case NEO_ASM_MEMBER_TAG: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_MEMBER_TAG %d,%d\n", line, column);
    } break;
    case NEO_ASM_PRIVATE_TAG: {
      char *constant = neo_string_encode_escape(
          allocator, neo_js_program_get_string(self, &offset));
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_PRIVATE_TAG %s,%d,%d\n", constant, line, column);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SUPER_MEMBER_TAG: {
      uint32_t line = neo_js_program_get_integer(self, &offset);
      uint32_t column = neo_js_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_SUPER_MEMBER_TAG %d,%d\n", line, column);
    } break;
    case NEO_ASM_DEL_FIELD: {
      fprintf(fp, "NEO_ASM_DEL_FIELD\n");
    } break;
    default:
      return neo_create_error_node(allocator, "Invalid operator");
    }
  }
  return NULL;
}