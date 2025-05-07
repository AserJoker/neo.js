#include "core/error.h"
#include "core/allocator.h"
#include "core/list.h"
#include <stdio.h>
#include <string.h>
struct _neo_error_t {
  neo_list_t stack;
  char *message;
  char *type;
};

typedef struct _neo_error_frame_t {
  const char *filename;
  const char *funcname;
  int32_t line;
} *neo_error_frame_t;

static neo_error_t g_error = NULL;

static neo_allocator_t g_allocator = NULL;

static void neo_error_dispose(neo_allocator_t allocator, neo_error_t error) {
  neo_allocator_free(allocator, error->type);
  neo_allocator_free(allocator, error->message);
  neo_allocator_free(allocator, error->stack);
}

void neo_error_initialize(neo_allocator_t allocator) {
  g_allocator = allocator;
}

bool neo_has_error() { return g_error != NULL; }

void neo_push_error(const char *type, const char *message) {
  if (g_error) {
    neo_allocator_free(g_allocator, g_error);
  }
  g_error = (neo_error_t)neo_allocator_alloc(
      g_allocator, sizeof(struct _neo_error_t), neo_error_dispose);
  neo_list_initialize_t initialize;
  initialize.auto_free = true;
  g_error->stack = neo_create_list(g_allocator, &initialize);
  size_t message_len = strlen(message);
  g_error->message =
      (char *)neo_allocator_alloc(g_allocator, message_len + 1, NULL);
  g_error->message[message_len] = 0;
  strcpy(g_error->message, message);
  size_t type_len = strlen(type);
  g_error->type = (char *)neo_allocator_alloc(g_allocator, type_len, NULL);
  g_error->type[type_len] = 0;
  strcpy(g_error->type, type);
}

void neo_push_stack(const char *funcname, const char *filename, int32_t line) {
  neo_error_frame_t stack =
      neo_allocator_alloc(g_allocator, sizeof(struct _neo_error_frame_t), NULL);
  stack->filename = filename;
  stack->funcname = funcname;
  stack->line = line;
  neo_list_push(g_error->stack, stack);
}

neo_error_t neo_poll_error(const char *funcname, const char *filename,
                           int32_t line) {
  neo_push_stack(funcname, filename, line);
  neo_error_t current = g_error;
  g_error = NULL;
  return current;
}

const char *neo_error_get_type(neo_error_t self) { return self->type; }

const char *neo_error_get_message(neo_error_t self) { return self->message; }

char *neo_error_to_string(neo_error_t self) {
  char *result = NULL;
  size_t len = 0;
  neo_list_node_t node = neo_list_get_last(self->stack);
  while (node != neo_list_get_head(self->stack)) {
    neo_error_frame_t frame = (neo_error_frame_t)neo_list_node_get(node);
    size_t line_len = 0;
    if (frame->filename) {
      line_len = strlen(frame->filename) + strlen(frame->funcname) + 16;
      char *line =
          (char *)neo_allocator_alloc(g_allocator, line_len + len, NULL);
      if (result) {
        sprintf(line, "  at %s (%s:%d)\n%s", frame->funcname, frame->filename,
                frame->line, result);
        neo_allocator_free(g_allocator, result);
      } else {
        sprintf(line, "  at %s (%s:%d)", frame->funcname, frame->filename,
                frame->line);
      }
      result = line;
      len += line_len;
    } else {
      line_len = strlen(frame->funcname) + 16;
      char *line =
          (char *)neo_allocator_alloc(g_allocator, line_len + len, NULL);
      if (result) {
        sprintf(line, "  at %s\n%s", frame->funcname, result);
        neo_allocator_free(g_allocator, result);
      } else {
        sprintf(line, "  at %s", frame->funcname);
      }
      result = line;
      len += line_len;
    }
    node = neo_list_node_last(node);
  }
  size_t line_len = strlen(self->type) + strlen(self->message) + 16;
  char *line = neo_allocator_alloc(g_allocator, len + line_len, NULL);
  if (result) {
    sprintf(line, "%s: %s\n%s", self->type, self->message, result);
    neo_allocator_free(g_allocator, result);
  } else {
    sprintf(line, "%s: %s", self->type, self->message);
  }
  result = line;
  return result;
}
