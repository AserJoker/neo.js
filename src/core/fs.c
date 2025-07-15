#include "core/fs.h"
#include "core/allocator.h"
#include "core/unicode.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#define S_ISDIR(m) (((m) & 0170000) == (0040000))
#else
#include <unistd.h>
#endif
#include <wchar.h>

char *neo_fs_read_file(neo_allocator_t allocator, const wchar_t *filename) {
  char *file = neo_wstring_to_string(allocator, filename);
  FILE *fp = fopen(file, "r");
  if (fp == NULL) {
    goto onerror;
  }
  neo_allocator_free(allocator, file);

  size_t len = 0;
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *buf = neo_allocator_alloc(allocator, len + 1, NULL);
  memset(buf, 0, len);
  fread(buf, len, 1, fp);
  fclose(fp);
  return buf;
onerror:
  if (fp) {
    fclose(fp);
  }
  neo_allocator_free(allocator, file);
  return NULL;
}

bool neo_fs_is_dir(neo_allocator_t allocator, const wchar_t *filename) {
  char *path = neo_wstring_to_string(allocator, filename);
  struct stat st;
  if (stat(path, &st) != 0) {
    return false;
  }
  neo_allocator_free(allocator, path);
  return S_ISDIR(st.st_mode);
}

bool neo_fs_exist(neo_allocator_t allocator, const wchar_t *filename) {
  char *path = neo_wstring_to_string(allocator, filename);
  struct stat st;
  if (stat(path, &st) == -1) {
    neo_allocator_free(allocator, path);
    return false;
  } else {
    neo_allocator_free(allocator, path);
    return true;
  }
}