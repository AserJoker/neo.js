#include "core/path.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/string.h"
#include "core/unicode.h"
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#ifdef _WIN32
#define DEFAULT_PATH_SPLITER '\\'
#else
#define DEFAULT_PATH_SPLITER '/'
#endif

struct _neo_path_t {
  neo_list_t parts;
  neo_allocator_t allocator;
};

bool neo_path_append(neo_path_t path, wchar_t *part) {
  if (wcscmp(part, L".") == 0) {
    return false;
  }
  if (wcscmp(part, L"..") == 0) {
    if (neo_list_get_size(path->parts) == 0) {
      neo_list_push(path->parts, part);
      return true;
    }
    wchar_t *last = neo_list_node_get(neo_list_get_last(path->parts));
    if (wcscmp(last, L"..") == 0) {
      neo_list_push(path->parts, part);
      return true;
    }
    neo_list_pop(path->parts);
    return false;
  }
  neo_list_push(path->parts, part);
  return true;
}

static void neo_path_dispose(neo_allocator_t allocator, neo_path_t self) {
  neo_allocator_free(allocator, self->parts);
}

neo_path_t neo_create_path(neo_allocator_t allocator, const wchar_t *source) {
  neo_path_t path = neo_allocator_alloc(allocator, sizeof(struct _neo_path_t),
                                        neo_path_dispose);
  path->allocator = allocator;
  neo_list_initialize_t initialze = {true};
  path->parts = neo_create_list(allocator, &initialze);
  if (source) {
    wchar_t *part = NULL;
    size_t offset = 0;
    size_t len = wcslen(source) + 1;
    if (*source == '/') {
      neo_list_push(path->parts, neo_create_wstring(allocator, L"/"));
      source++;
    }
    if (*source) {
      part = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
      while (*source) {
        if (*source == '/' || *source == '\\') {
          part[offset] = 0;
          if (neo_path_append(path, part)) {
            part = neo_allocator_alloc(allocator, sizeof(wchar_t) * len, NULL);
          }
          offset = 0;
        } else {
          part[offset] = *source;
          offset++;
        }
        source++;
      }
      part[offset] = 0;
      if (offset > 0) {
        neo_list_push(path->parts, part);
      } else {
        neo_allocator_free(allocator, part);
      }
    }
  }
  return path;
}

neo_path_t neo_path_concat(neo_path_t current, neo_path_t another) {
  if (neo_list_get_size(another->parts)) {
    wchar_t *first = neo_list_node_get(neo_list_get_first(another->parts));
    if (wcscmp(first, L"/") == 0) {
      return neo_path_clone(another);
    }
    if (wcslen(first) > 1 && first[1] == ':') {
      return neo_path_clone(another);
    }
  }
  current = neo_path_clone(current);
  for (neo_list_node_t it = neo_list_get_first(another->parts);
       it != neo_list_get_tail(another->parts); it = neo_list_node_next(it)) {
    wchar_t *part = neo_list_node_get(it);
    part = neo_create_wstring(current->allocator, part);
    if (!neo_path_append(current, part)) {
      neo_allocator_free(current->allocator, part);
    }
  }
  return current;
}
neo_path_t neo_path_clone(neo_path_t current) {
  neo_path_t path = neo_create_path(current->allocator, NULL);
  for (neo_list_node_t it = neo_list_get_first(current->parts);
       it != neo_list_get_tail(current->parts); it = neo_list_node_next(it)) {
    neo_list_push(path->parts, neo_create_wstring(current->allocator,
                                                  neo_list_node_get(it)));
  }
  return path;
}

neo_path_t neo_path_current(neo_allocator_t allocator) {
  char *buf = getcwd(NULL, 0);
  if (!buf) {
    return NULL;
  }
  wchar_t *source = neo_string_to_wstring(allocator, buf);
  free(buf);
  neo_path_t path = neo_create_path(allocator, source);
  neo_allocator_free(allocator, source);
  return path;
}

neo_path_t neo_path_absolute(neo_path_t current) {
  neo_path_t cwd = neo_path_current(current->allocator);
  neo_path_t result = neo_path_concat(cwd, current);
  neo_allocator_free(cwd->allocator, cwd);
  neo_allocator_free(current->allocator, current);
  return result;
}

neo_path_t neo_path_parent(neo_path_t current) {
  if (neo_list_get_size(current->parts) < 1) {
    return NULL;
  }
  neo_path_t result = neo_create_path(current->allocator, NULL);
  for (neo_list_node_t it = neo_list_get_first(current->parts);
       it != neo_list_get_last(current->parts); it = neo_list_node_next(it)) {
    wchar_t *part = neo_list_node_get(it);
    neo_list_push(result->parts, neo_create_wstring(current->allocator, part));
  }
  return result;
}
const wchar_t *neo_path_filename(neo_path_t current) {
  if (neo_list_get_size(current->parts)) {
    return neo_list_node_get(neo_list_get_tail(current->parts));
  }
  return NULL;
}

wchar_t *neo_path_to_string(neo_path_t path) {
  wchar_t *result =
      neo_allocator_alloc(path->allocator, 128 * sizeof(wchar_t), NULL);
  result[0] = 0;
  size_t max = 128;
  for (neo_list_node_t it = neo_list_get_first(path->parts);
       it != neo_list_get_tail(path->parts); it = neo_list_node_next(it)) {
    wchar_t *part = neo_list_node_get(it);
    result = neo_wstring_concat(path->allocator, result, &max, part);
    if (it != neo_list_get_last(path->parts) && wcscmp(part, L"/") != 0) {
      wchar_t spliter[2] = {DEFAULT_PATH_SPLITER, 0};
      result = neo_wstring_concat(path->allocator, result, &max, spliter);
    }
  }
  return result;
}