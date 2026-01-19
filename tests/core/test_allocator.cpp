#include "neojs/core/allocator.h"
#include <cstdint>
#include <gtest/gtest.h>

class neo_test_allocator : public testing::Test {};

TEST_F(neo_test_allocator, alloc_and_free) {
  neo_allocator_t allocator = neo_create_allocator(NULL);
  void *data = neo_allocator_alloc(allocator, sizeof(uint32_t), NULL);
  neo_allocator_free(allocator, data);
  neo_delete_allocator(allocator);
}

static uint32_t custom_dispose_value = 0;
TEST_F(neo_test_allocator, custom_dispose) {
  neo_allocator_t allocator = neo_create_allocator(NULL);
  uint32_t *data = (uint32_t *)neo_allocator_alloc(
      allocator, sizeof(uint32_t),
      +[](neo_allocator_t allocator, uint32_t *self) {
        custom_dispose_value = *self;
      });
  *data = 123;
  neo_allocator_free(allocator, data);
  neo_delete_allocator(allocator);
  ASSERT_EQ(custom_dispose_value, 123);
}

static size_t custom_alloc_and_free_alloc = 0;
TEST_F(neo_test_allocator, custom_alloc_and_free) {
  neo_allocator_initialize_t initialize = {};
  initialize.alloc = +[](size_t size) -> void * {
    custom_alloc_and_free_alloc = size;
    return ::operator new(size);
  };
  initialize.free = +[](void *ptr) {
    custom_alloc_and_free_alloc = 0;
    ::operator delete(ptr);
  };
  neo_allocator_t allocator = neo_create_allocator(&initialize);
  void *data = neo_allocator_alloc(allocator, sizeof(uint32_t), NULL);
  ASSERT_NE(custom_alloc_and_free_alloc, 0);
  neo_allocator_free(allocator, data);
  ASSERT_EQ(custom_alloc_and_free_alloc, 0);
  neo_delete_allocator(allocator);
}