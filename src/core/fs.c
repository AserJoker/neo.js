#include "core/fs.h"
#include "core/allocator.h"
#include "core/unicode.h"
#include <stdio.h>
#include <wchar.h>

wchar_t *neo_fs_read_file(neo_allocator_t allocator, const wchar_t *filename) {
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
  buf[len] = 0;
  fread(buf, len, 1, fp);
  fclose(fp);
  wchar_t *result = neo_string_to_wstring(allocator, buf);
  neo_allocator_free(allocator, buf);
  return result;
onerror:
  if (fp) {
    fclose(fp);
  }
  neo_allocator_free(allocator, file);
  return NULL;
}