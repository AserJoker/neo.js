#include "core/allocator.h"
#include <cstdlib>
#include <gtest/gtest.h>

class TestAllocator : public testing::Test {};

size_t g_count = 0;

static void *alloc_stub(size_t size) {
  g_count++;
  return ::operator new(size);
}

static void free_stub(void *ptr) {
  ::operator delete(ptr);
  g_count--;
}

TEST_F(TestAllocator, alloc_and_free) {
  noix_allocator_initialize_t initialize = {alloc_stub, free_stub};
  noix_allocator_t allocator = noix_create_allocator(&initialize);
  int *data = (int *)noix_allocator_alloc(allocator, sizeof(int), NULL);
  noix_allocator_free(allocator, data);
  noix_delete_allocator(allocator);
  ASSERT_EQ(g_count, 0);
}

static int g_data = 0;

static void desc(noix_allocator_t allocator, int *data) { g_data = *data; }

TEST_F(TestAllocator, desctructor) {
  noix_allocator_t allocator = noix_create_default_allocator();
  int *data = (int *)noix_allocator_alloc(allocator, sizeof(int), desc);
  *data = 123;
  noix_allocator_free(allocator, data);
  noix_delete_allocator(allocator);
  ASSERT_EQ(g_data, 123);
}