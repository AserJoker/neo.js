// allocator_test.cpp - Allocator 单元测试
//
// 验证 MallocAllocator 真实分配/释放，以及接口可注入 mock（可测试性）。
//
// 关联 ADR: 008（Allocator）、027（FFI 封装/可测试性）、028（infra）

#include "infra/allocator.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using neo::infra::Allocator;
using neo::infra::MallocAllocator;

// 记录调用次数的 mock，验证业务代码通过接口使用分配器
class MockAllocator : public Allocator {
public:
    struct Call {
        enum Kind { Alloc, AllocAligned, Free, FreeAligned } kind;
        size_t bytes   = 0;
        size_t align   = 0;
        void*  ptr     = nullptr;
    };
    std::vector<Call> calls;

    void* alloc(size_t bytes) override {
        calls.push_back({Call::Alloc, bytes, 0, nullptr});
        return raw_.alloc(bytes);
    }
    void* alloc_aligned(size_t bytes, size_t align) override {
        calls.push_back({Call::AllocAligned, bytes, align, nullptr});
        return raw_.alloc_aligned(bytes, align);
    }
    void free(void* p) override {
        calls.push_back({Call::Free, 0, 0, p});
        raw_.free(p);
    }
    void free_aligned(void* p) override {
        calls.push_back({Call::FreeAligned, 0, 0, p});
        raw_.free_aligned(p);
    }

private:
    MallocAllocator raw_;
};

TEST(MallocAllocatorTest, AllocAndFree) {
    MallocAllocator a;
    void* p = a.alloc(128);
    ASSERT_NE(p, nullptr);
    // 内存可写可读（验证有效内存）
    auto* bytes = static_cast<uint8_t*>(p);
    bytes[0]   = 0xAB;
    bytes[127] = 0xCD;
    EXPECT_EQ(bytes[0], 0xAB);
    EXPECT_EQ(bytes[127], 0xCD);
    a.free(p);
}

TEST(MallocAllocatorTest, AllocZeroReturnsNonNull) {
    MallocAllocator a;
    void* p = a.alloc(0);
    EXPECT_NE(p, nullptr);  // 统一返回非空哨兵
    a.free(p);
}

TEST(MallocAllocatorTest, AllocAlignedSatisfiesAlignment) {
    MallocAllocator a;
    constexpr size_t kAlign = 64;
    void* p = a.alloc_aligned(256, kAlign);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % kAlign, 0u);
    a.free_aligned(p);
}

TEST(MallocAllocatorTest, AllocAlignedMultiple) {
    MallocAllocator a;
    for (size_t align : {16u, 32u, 64u, 128u}) {
        void* p = a.alloc_aligned(100, align);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % align, 0u);
        a.free_aligned(p);
    }
}

TEST(AllocatorInterfaceTest, MockRecordsCalls) {
    MockAllocator mock;
    void* p = mock.alloc(64);
    ASSERT_NE(p, nullptr);
    mock.free(p);

    ASSERT_EQ(mock.calls.size(), 2u);
    EXPECT_EQ(mock.calls[0].kind, MockAllocator::Call::Alloc);
    EXPECT_EQ(mock.calls[0].bytes, 64u);
    EXPECT_EQ(mock.calls[1].kind, MockAllocator::Call::Free);
    EXPECT_EQ(mock.calls[1].ptr, p);
}

TEST(AllocatorInterfaceTest, MockRecordsAligned) {
    MockAllocator mock;
    void* p = mock.alloc_aligned(256, 32);
    ASSERT_NE(p, nullptr);
    mock.free_aligned(p);

    ASSERT_EQ(mock.calls.size(), 2u);
    EXPECT_EQ(mock.calls[0].kind, MockAllocator::Call::AllocAligned);
    EXPECT_EQ(mock.calls[0].bytes, 256u);
    EXPECT_EQ(mock.calls[0].align, 32u);
    EXPECT_EQ(mock.calls[1].kind, MockAllocator::Call::FreeAligned);
}

// 业务代码使用模式示例：通过 Allocator* 接口注入
class Buffer {
    Allocator* alloc_;
    void*      data_;
public:
    Buffer(Allocator* a, size_t n) : alloc_(a), data_(a->alloc(n)) {}
    ~Buffer() { alloc_->free(data_); }
    void* data() { return data_; }
};

TEST(AllocatorUsagePattern, BufferUsesInjectedAllocator) {
    MockAllocator mock;
    {
        Buffer buf(&mock, 512);
        ASSERT_NE(buf.data(), nullptr);
    }
    // 构造时 alloc，析构时 free
    ASSERT_EQ(mock.calls.size(), 2u);
    EXPECT_EQ(mock.calls[0].kind, MockAllocator::Call::Alloc);
    EXPECT_EQ(mock.calls[1].kind, MockAllocator::Call::Free);
}

}  // namespace
