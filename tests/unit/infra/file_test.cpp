// file_test.cpp - File 单元测试
//
// 验证 MemoryFileSystem（无 I/O，纯内存）和 DiskFileSystem（临时文件集成测试）。
//
// 关联 ADR: 027（FFI 封装/可测试性）、028（infra）

#include "infra/file.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <span>
#include <string>

namespace {

using neo::infra::DiskFileSystem;
using neo::infra::ErrCode;
using neo::infra::File;
using neo::infra::FileSystem;
using neo::infra::MemoryFileSystem;
using neo::infra::OpenMode;
using neo::infra::Result;

// =========================================================================
// MemoryFileSystem 单元测试（无磁盘 I/O）
// =========================================================================

TEST(MemoryFileSystemTest, AddAndExists) {
    MemoryFileSystem fs;
    fs.add_file("a.txt", "hello");
    auto r = fs.exists("a.txt");
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value());
    auto r2 = fs.exists("missing.txt");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_FALSE(r2.value());
}

TEST(MemoryFileSystemTest, OpenReadReturnsContent) {
    MemoryFileSystem fs;
    fs.add_file("data.txt", "abc123");
    auto r = fs.open("data.txt", OpenMode::Read);
    ASSERT_TRUE(r.is_ok());
    auto& f = r.value();
    auto content = f.read_all();
    ASSERT_TRUE(content.is_ok());
    EXPECT_EQ(content.value(), "abc123");
    f.close();
}

TEST(MemoryFileSystemTest, OpenReadMissingReturnsErr) {
    MemoryFileSystem fs;
    auto r = fs.open("nope.txt", OpenMode::Read);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::NotFound);
}

TEST(MemoryFileSystemTest, WriteCreatesAndPersists) {
    MemoryFileSystem fs;
    auto r = fs.open("out.txt", OpenMode::Write);
    ASSERT_TRUE(r.is_ok());
    auto w = r.value().write_all("written content");
    ASSERT_TRUE(w.is_ok());
    r.value().close();  // 回写到 fs
    EXPECT_EQ(fs.get_content("out.txt"), "written content");
}

TEST(MemoryFileSystemTest, AppendAddsToExisting) {
    MemoryFileSystem fs;
    fs.add_file("log.txt", "line1\n");
    {
        auto r = fs.open("log.txt", OpenMode::Append);
        ASSERT_TRUE(r.is_ok());
        r.value().write_all("line2\n");
        r.value().close();
    }
    EXPECT_EQ(fs.get_content("log.txt"), "line1\nline2\n");
}

TEST(MemoryFileSystemTest, RemoveMarksNonExistent) {
    MemoryFileSystem fs;
    fs.add_file("temp.txt", "data");
    ASSERT_TRUE(fs.remove("temp.txt").is_ok());
    auto e = fs.exists("temp.txt");
    ASSERT_TRUE(e.is_ok());
    EXPECT_FALSE(e.value());
}

TEST(MemoryFileSystemTest, RemoveMissingReturnsErr) {
    MemoryFileSystem fs;
    auto r = fs.remove("nope.txt");
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::NotFound);
}

TEST(MemoryFileSystemTest, ReadPartial) {
    MemoryFileSystem fs;
    fs.add_file("buf.dat", "0123456789");
    auto r = fs.open("buf.dat", OpenMode::Read);
    ASSERT_TRUE(r.is_ok());
    char buf[4];
    auto got = r.value().read(std::as_writable_bytes(std::span<char>(buf)));
    ASSERT_TRUE(got.is_ok());
    EXPECT_EQ(got.value(), 4u);
    EXPECT_EQ(std::memcmp(buf, "0123", 4), 0);
    r.value().close();
}

// =========================================================================
// DiskFileSystem 集成测试（用临时文件）
// =========================================================================

namespace fs = std::filesystem;

class DiskFileSystemTest : public ::testing::Test {
protected:
    std::string tmp_path;

    void SetUp() override {
        // 用静态计数确保唯一（避免 getpid 跨平台问题）
        static int counter = 0;
        tmp_path = (fs::temp_directory_path() /
                    ("neojs_test_" + std::to_string(counter++))).string();
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove(tmp_path, ec);
    }
};

TEST_F(DiskFileSystemTest, WriteThenRead) {
    DiskFileSystem dfs;
    {
        auto r = dfs.open(tmp_path, OpenMode::Write);
        ASSERT_TRUE(r.is_ok());
        auto w = r.value().write_all("disk content");
        ASSERT_TRUE(w.is_ok());
        r.value().close();
    }
    {
        auto r = dfs.open(tmp_path, OpenMode::Read);
        ASSERT_TRUE(r.is_ok());
        auto content = r.value().read_all();
        ASSERT_TRUE(content.is_ok());
        EXPECT_EQ(content.value(), "disk content");
        r.value().close();
    }
}

TEST_F(DiskFileSystemTest, OpenReadMissingReturnsErr) {
    DiskFileSystem dfs;
    auto r = dfs.open("definitely_nonexistent_file.txt", OpenMode::Read);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::NotFound);
}

TEST_F(DiskFileSystemTest, ExistsChecksFile) {
    DiskFileSystem dfs;
    {
        auto r = dfs.open(tmp_path, OpenMode::Write);
        ASSERT_TRUE(r.is_ok());
        r.value().write_all("x");
        r.value().close();
    }
    auto e = dfs.exists(tmp_path);
    ASSERT_TRUE(e.is_ok());
    EXPECT_TRUE(e.value());

    auto e2 = dfs.exists(tmp_path + ".missing");
    ASSERT_TRUE(e2.is_ok());
    EXPECT_FALSE(e2.value());
}

TEST_F(DiskFileSystemTest, RemoveDeletesFile) {
    DiskFileSystem dfs;
    {
        auto r = dfs.open(tmp_path, OpenMode::Write);
        ASSERT_TRUE(r.is_ok());
        r.value().write_all("data");
        r.value().close();
    }
    ASSERT_TRUE(dfs.remove(tmp_path).is_ok());
    auto e = dfs.exists(tmp_path);
    ASSERT_TRUE(e.is_ok());
    EXPECT_FALSE(e.value());
}

}  // namespace
