#include "core/error.h"
#include "core/allocator.h"
#include "core/list.h"
#include <stdio.h>
#include <string.h>
struct _noix_error_t {
  noix_list_t stack;
  char *message;
  char *type;
};

typedef struct _noix_error_frame_t {
  const char *filename;
  const char *funcname;
  int32_t line;
} *noix_error_frame_t;

static noix_error_t g_error = NULL;

static noix_allocator_t g_allocator = NULL;

static void noix_error_dispose(noix_allocator_t allocator, noix_error_t error) {
  noix_allocator_free(allocator, error->type);
  noix_allocator_free(allocator, error->message);
  noix_allocator_free(allocator, error->stack);
}

void noix_error_initialize(noix_allocator_t allocator) {
  g_allocator = allocator;
}

bool noix_has_error() { return g_error != NULL; }

void noix_push_error(const char *type, const char *message) {
  if (g_error) {
    noix_allocator_free(g_allocator, g_error);
  }
  g_error = (noix_error_t)noix_allocator_alloc(
      g_allocator, sizeof(struct _noix_error_t), noix_error_dispose);
  noix_list_initialize initialize;
  initialize.auto_free = true;
  g_error->stack = noix_create_list(g_allocator, &initialize);
  size_t message_len = strlen(message);
  g_error->message =
      (char *)noix_allocator_alloc(g_allocator, message_len + 1, NULL);
  g_error->message[message_len] = 0;
  strcpy(g_error->message, message);
  size_t type_len = strlen(type);
  g_error->type = (char *)noix_allocator_alloc(g_allocator, type_len, NULL);
  g_error->type[type_len] = 0;
  strcpy(g_error->type, type);
}

void noix_push_stack(const char *funcname, const char *filename, int32_t line) {
  noix_error_frame_t stack = noix_allocator_alloc(
      g_allocator, sizeof(struct _noix_error_frame_t), NULL);
  stack->filename = filename;
  stack->funcname = funcname;
  stack->line = line;
  noix_list_push(g_error->stack, stack);
}

noix_error_t noix_poll_error(const char *funcname, const char *filename,
                             int32_t line) {
  noix_push_stack(funcname, filename, line);
  noix_error_t current = g_error;
  g_error = NULL;
  return current;
}

const char *noix_error_get_type(noix_error_t self) { return self->type; }

const char *noix_error_get_message(noix_error_t self) { return self->message; }

char *noix_error_to_string(noix_error_t self) {
  char *result = NULL;
  size_t len = 0;
  noix_list_node_t node = noix_list_get_last(self->stack);
  while (node != noix_list_get_head(self->stack)) {
    noix_error_frame_t frame = (noix_error_frame_t)noix_list_node_get(node);
    size_t line_len = 0;
    if (frame->filename) {
      line_len = strlen(frame->filename) + strlen(frame->funcname) + 16;
      char *line =
          (char *)noix_allocator_alloc(g_allocator, line_len + len, NULL);
      if (result) {
        sprintf(line, "  at %s (%s:%d)\n%s", frame->funcname, frame->filename,
                frame->line, result);
        noix_allocator_free(g_allocator, result);
      } else {
        sprintf(line, "  at %s (%s:%d)", frame->funcname, frame->filename,
                frame->line);
      }
      result = line;
      len += line_len;
    } else {
      line_len = strlen(frame->funcname) + 16;
      char *line =
          (char *)noix_allocator_alloc(g_allocator, line_len + len, NULL);
      if (result) {
        sprintf(line, "  at %s\n%s", frame->funcname, result);
        noix_allocator_free(g_allocator, result);
      } else {
        sprintf(line, "  at %s", frame->funcname);
      }
      result = line;
      len += line_len;
    }
    node = noix_list_node_last(node);
  }
  size_t line_len = strlen(self->type) + strlen(self->message) + 16;
  char *line = noix_allocator_alloc(g_allocator, len + line_len, NULL);
  if (result) {
    sprintf(line, "%s: %s\n%s", self->type, self->message, result);
    noix_allocator_free(g_allocator, result);
  } else {
    sprintf(line, "%s: %s", self->type, self->message);
  }
  result = line;
  return result;
}
