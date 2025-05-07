#include "core/allocator.h"
#include <cstdlib>
#include <gtest/gtest.h>

class TEST_allocator : public testing::Test {};

size_t g_count = 0;

static void *alloc_stub(size_t size) {
  g_count++;
  return ::operator new(size);
}

static void free_stub(void *ptr) {
  ::operator delete(ptr);
  g_count--;
}

TEST_F(TEST_allocator, alloc_and_free) {
  neo_allocator_initialize_t initialize = {alloc_stub, free_stub};
  neo_allocator_t allocator = neo_create_allocator(&initialize);
  int *data = (int *)neo_allocator_alloc(allocator, sizeof(int), NULL);
  neo_allocator_free(allocator, data);
  neo_delete_allocator(allocator);
  ASSERT_EQ(g_count, 0);
}

static int g_data = 0;

static void desc(neo_allocator_t allocator, int *data) { g_data = *data; }

TEST_F(TEST_allocator, desctructor) {
  neo_allocator_t allocator = neo_create_default_allocator();
  int *data = (int *)neo_allocator_alloc(allocator, sizeof(int), desc);
  *data = 123;
  neo_allocator_free(allocator, data);
  neo_delete_allocator(allocator);
  ASSERT_EQ(g_data, 123);
}