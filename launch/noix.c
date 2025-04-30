#include "core/allocator.h"
#include <stdio.h>

typedef struct _test {
  int a;
} *test;

void destructor_test(noix_allocator_t allocator, test self) {
  printf("%d\n", self->a);
}

int main(int argc, char *argv[]) {
  noix_allocator_t allocator = noix_create_default_allocator();
  test t =
      noix_allocator_alloc(allocator, sizeof(struct _test), destructor_test);
  t->a = 123;
  noix_allocator_free(allocator, t);
  noix_delete_allocator(allocator);
  return 0;
}