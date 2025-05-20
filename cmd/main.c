
#include "compiler/node.h"
#include "compiler/parser.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/json.h"
#include "core/string.h"
#include "core/variable.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_error_initialize(allocator);
  if (argc > 1) {
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
      fprintf(stderr, "cannot open file: %s\n", argv[1]);
      return -1;
    }
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    char *buf = (char *)neo_allocator_alloc(allocator, size + 1, NULL);
    memset(buf, 0, size + 1);
    fseek(fp, 0, SEEK_SET);
    fread(buf, size, 1, fp);
    fclose(fp);
    neo_ast_node_t node = neo_ast_parse_code(allocator, argv[1], buf);
    if (neo_has_error()) {
      neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
      char *msg = neo_error_to_string(error);
      fprintf(stderr, "%s\n", msg);
      neo_allocator_free(allocator, msg);
      neo_allocator_free(allocator, error);
    } else {
      neo_variable_t variable = neo_ast_node_serialize(allocator, node);
      neo_string_t json = neo_json_stringify(allocator, variable);
      printf("%s\n", neo_string_get(json));
      neo_allocator_free(allocator, json);
      neo_allocator_free(allocator, variable);
    }
    neo_allocator_free(allocator, node);
    neo_allocator_free(allocator, buf);
  }
  neo_delete_allocator(allocator);
  return 0;
}