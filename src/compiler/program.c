#include "compiler/program.h"
#include "compiler/asm.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/list.h"
#include "core/path.h"
#include "core/string.h"
#include <string.h>
#include <wchar.h>

static void neo_program_dispose(neo_allocator_t allocator,
                                neo_program_t program) {
  neo_allocator_free(allocator, program->constants);
  neo_allocator_free(allocator, program->codes);
  neo_allocator_free(allocator, program->filename);
  neo_allocator_free(allocator, program->dirname);
}

neo_program_t neo_create_program(neo_allocator_t allocator,
                                 const wchar_t *file) {
  neo_program_t program = (neo_program_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_program_t), neo_program_dispose);
  program->filename = neo_create_wstring(allocator, file);
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

size_t neo_program_add_constant(neo_allocator_t allocator, neo_program_t self,
                                const wchar_t *constant) {
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(self->constants);
       it != neo_list_get_tail(self->constants); it = neo_list_node_next(it)) {
    if (wcscmp(neo_list_node_get(it), constant) == 0) {
      return idx;
    }
    idx++;
  }
  neo_list_push(self->constants, neo_create_wstring(allocator, constant));
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
                            const wchar_t *string) {
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

static const wchar_t *neo_program_get_string(neo_program_t program,
                                             size_t *offset) {
  size_t idx = *(size_t *)((char *)neo_buffer_get(program->codes) + *offset);
  *offset += sizeof(size_t);
  neo_list_node_t it = neo_list_get_first(program->constants);
  for (size_t i = 0; i < idx; i++) {
    it = neo_list_node_next(it);
  }
  return (const wchar_t *)neo_list_node_get(it);
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
  fprintf(fp, ".filename: %ls\n", self->filename);
  fprintf(fp, ".dirname: %ls\n", self->dirname);
  size_t idx = 0;
  fprintf(fp, "[section .constants]\n");
  for (neo_list_node_t it = neo_list_get_first(self->constants);
       it != neo_list_get_tail(self->constants); it = neo_list_node_next(it)) {
    const wchar_t *current = neo_list_node_get(it);
    wchar_t *constant = neo_wstring_encode(allocator, current);
    fprintf(fp, ".%zu: \"%ls\"\n", idx, constant);
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
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_STORE \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SAVE: {
      fprintf(fp, "NEO_ASM_SAVE\n");
    } break;
    case NEO_ASM_DEF: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DEF \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_LOAD: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_LOAD \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_CLONE: {
      fprintf(fp, "NEO_ASM_CLONE\n");
    } break;
    case NEO_ASM_INIT_FIELD:
      fprintf(fp, "NEO_ASM_INIT_FIELD\n");
      break;
    case NEO_ASM_INIT_PRIVATE_FIELD:
      fprintf(fp, "NEO_ASM_INIT_PRIVATE_FIELD\n");
      break;
    case NEO_ASM_INIT_ACCESSOR:
      fprintf(fp, "NEO_ASM_INIT_ACCESSOR\n");
      break;
    case NEO_ASM_INIT_PRIVATE_ACCESSOR:
      fprintf(fp, "NEO_ASM_INIT_PRIVATE_ACCESSOR\n");
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
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_STRING \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_BIGINT: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_BIGINT \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_REGEXP: {
      wchar_t *content =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      wchar_t *flag =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_REGEXP \"%ls\",\"%ls\"\n", content, flag);
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
      uint32_t line = neo_program_get_integer(self, &offset);
      uint32_t column = neo_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_SUPER_CALL %d,%d\n", line, column);
    } break;
    case NEO_ASM_SUPER_MEMBER_CALL: {
      uint32_t line = neo_program_get_integer(self, &offset);
      uint32_t column = neo_program_get_integer(self, &offset);
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
              neo_program_get_integer(self, &offset));
      break;
    case NEO_ASM_SET_NAME: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_NAME \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_SET_SOURCE: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_SOURCE \"%ls\"\n", constant);
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
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_SET_CLOSURE: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_SET_CLOSURE \"%ls\"\n", constant);
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
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_DIRECTIVE \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_CALL: {
      uint32_t line = neo_program_get_integer(self, &offset);
      uint32_t column = neo_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_CALL %d,%d\n", line, column);
    } break;
    case NEO_ASM_EVAL: {
      uint32_t line = neo_program_get_integer(self, &offset);
      uint32_t column = neo_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_EVAL %d,%d\n", line, column);
    } break;
    case NEO_ASM_MEMBER_CALL: {
      uint32_t line = neo_program_get_integer(self, &offset);
      uint32_t column = neo_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_MEMBER_CALL %d,%d\n", line, column);
    } break;
    case NEO_ASM_GET_FIELD:
      fprintf(fp, "NEO_ASM_GET_FIELD\n");
      break;
    case NEO_ASM_SET_FIELD:
      fprintf(fp, "NEO_ASM_SET_FIELD\n");
      break;
    case NEO_ASM_PRIVATE_MEMBER_CALL: {
      uint32_t line = neo_program_get_integer(self, &offset);
      uint32_t column = neo_program_get_integer(self, &offset);
      fprintf(fp, "NEO_ASM_PRIVATE_MEMBER_CALL %d,%d\n", line, column);
    } break;
    case NEO_ASM_GET_PRIVATE_FIELD:
      fprintf(fp, "NEO_ASM_GET_PRIVATE_FIELD\n");
      break;
    case NEO_ASM_SET_PRIVATE_FIELD:
      fprintf(fp, "NEO_ASM_SET_PRIVATE_FIELD\n");
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
    case NEO_ASM_DEF_PRIVATE_GETTER:
      fprintf(fp, "NEO_ASM_DEF_PRIVATE_GETTER\n");
      break;
    case NEO_ASM_DEF_PRIVATE_SETTER:
      fprintf(fp, "NEO_ASM_DEF_PRIVATE_SETTER\n");
      break;
    case NEO_ASM_DEF_PRIVATE_METHOD:
      fprintf(fp, "NEO_ASM_DEF_PRIVATE_METHOD\n");
      break;
    case NEO_ASM_JNULL:
      fprintf(fp, "NEO_ASM_JNULL %zu\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_JNOT_NULL:
      fprintf(fp, "NEO_ASM_JNOT_NULL %zu\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_JFALSE:
      fprintf(fp, "NEO_ASM_JFALSE %zu\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_JTRUE:
      fprintf(fp, "NEO_ASM_JTRUE %zu\n",
              neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_JMP:
      fprintf(fp, "NEO_ASM_JMP %zu\n", neo_program_get_address(self, &offset));
      break;
    case NEO_ASM_BREAK: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_BREAK \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_CONTINUE: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_CONTINUE \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_THROW:
      fprintf(fp, "NEO_ASM_THROW\n");
      break;
    case NEO_ASM_TRY_BEGIN: {
      size_t onerror = neo_program_get_address(self, &offset);
      size_t onfinish = neo_program_get_address(self, &offset);
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
    case NEO_ASM_AWAIT_NEXT:
      fprintf(fp, "NEO_ASM_AWAIT_NEXT\n");
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
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_BREAK_LABEL \"%ls\",%zu\n", constant,
              neo_program_get_address(self, &offset));
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_PUSH_CONTINUE_LABEL: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_PUSH_CONTINUE_LABEL \"%ls\",%zu\n", constant,
              neo_program_get_address(self, &offset));
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_POP_LABEL:
      fprintf(fp, "NEO_ASM_POP_LABEL\n");
      break;
    case NEO_ASM_IMPORT: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_IMPORT \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_EXPORT: {
      wchar_t *constant =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_EXPORT \"%ls\"\n", constant);
      neo_allocator_free(allocator, constant);
    } break;
    case NEO_ASM_EXPORT_ALL:
      fprintf(fp, "NEO_ASM_EXPORT_ALL\n");
      break;
    case NEO_ASM_ASSERT: {
      wchar_t *type =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      wchar_t *value =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      wchar_t *module =
          neo_wstring_encode(allocator, neo_program_get_string(self, &offset));
      fprintf(fp, "NEO_ASM_ASSERT \"%ls\",\"%ls\",\"%ls\"\n", type, value,
              module);
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
      uint32_t line = neo_program_get_integer(self, &offset);
      uint32_t column = neo_program_get_integer(self, &offset);
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
    default:
      THROW("Invalid operator");
      return;
    }
  }
}