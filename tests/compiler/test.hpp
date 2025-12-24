#pragma once
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/location.h"
#include <gtest/gtest.h>
class neo_test : public testing::Test {
protected:
  neo_allocator_t allocator = NULL;
  neo_compile_scope_t scope = NULL;

public:
  void SetUp() override {
    allocator = neo_create_allocator(NULL);
    scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION, false,
                                   false);
  }
  void TearDown() override {
    neo_compile_scope_t current_scope = neo_compile_scope_pop(scope);
    neo_allocator_free(allocator, current_scope);
    neo_delete_allocator(allocator);
    allocator = NULL;
  }
};
neo_location_t create_location(const char *src);