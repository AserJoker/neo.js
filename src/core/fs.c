#include "neo.js/core/fs.h"
#include "neo.js/core/allocator.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#define S_ISDIR(m) (((m) & 0170000) == (0040000))
#else
#include <unistd.h>
#endif
#include <wchar.h>

char *neo_fs_read_file(neo_allocator_t allocator, const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    goto onerror;
  }

  size_t len = 0;
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *buf = neo_allocator_alloc(allocator, len + 1, NULL);
  memset(buf, 0, len + 1);
  fread(buf, len, 1, fp);
  buf[len] = 0;
  fclose(fp);
  return buf;
onerror:
  if (fp) {
    fclose(fp);
  }
  return NULL;
}

bool neo_fs_is_dir(const char *filename) {
  struct stat st;
  if (stat(filename, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

bool neo_fs_exist(const char *filename) {
  struct stat st;
  if (stat(filename, &st) == -1) {
    return false;
  } else {
    return true;
  }
}