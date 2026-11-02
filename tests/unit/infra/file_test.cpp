// file_test.cpp - File 单元测试
//
// 验证 MemoryFileSystem（无 I/O，纯内存）和 DiskFileSystem（构建目录临时文件）。
//
// 关联 ADR: 010（接口-实现-工厂，测试 mock）、027（FFI 封装/可测试性）、028（infra）

#include "infra/file.h"

#include "memory_filesystem.h"  // 测试用 mock

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <span>
#include <string>

namespace {

using neo::infra::ErrCode;
using neo::infra::File;
using neo::infra::FileSystem;
using neo::infra::OpenMode;
using neo::infra::Result;
using neo::infra::create_disk_filesystem;
using neo::infra::test::MemoryFileSystem;

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
    auto content = r.value().read_all();
    ASSERT_TRUE(content.is_ok());
    EXPECT_EQ(content.value(), "abc123");
    r.value().close();
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
    r.value().close();  // on_close 回写到 fs
    EXPECT_EQ(fs.get_content("out.txt"), "written content");
}

TEST(MemoryFileSystemTest, ReadWriteSameFileRoundTrip) {
    // ReadWrite 模式：同一 File 写入后，重置读指针读回（不跨 File 持久化）
    MemoryFileSystem fs;
    fs.add_file("rw.txt", "");
    auto r = fs.open("rw.txt", OpenMode::ReadWrite);
    ASSERT_TRUE(r.is_ok());
    auto w = r.value().write_all("rw content");
    ASSERT_TRUE(w.is_ok());
    // 读指针重置后读回
    r.value().close();
    auto r2 = fs.open("rw.txt", OpenMode::Read);
    ASSERT_TRUE(r2.is_ok());
    auto content = r2.value().read_all();
    ASSERT_TRUE(content.is_ok());
    EXPECT_EQ(content.value(), "rw content");
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
    // on_close 回写：追加后的内容含 line2
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
// DiskFileSystem 集成测试（临时文件存于构建目录下 tmp/，不污染系统 temp）
// =========================================================================

namespace fs = std::filesystem;

class DiskFileSystemTest : public ::testing::Test {
protected:
    std::string tmp_path;
    static std::string tmp_dir_;

    static void SetUpTestSuite() {
        // NEO_TEST_TMP_DIR 由 CMake 编译期宏注入（指向 <build-dir>/tmp）
        tmp_dir_ = NEO_TEST_TMP_DIR;
        std::error_code ec;
        fs::create_directories(tmp_dir_, ec);
    }

    void SetUp() override {
        static int counter = 0;
        tmp_path = (fs::path(tmp_dir_) /
                    ("neojs_test_" + std::to_string(counter++))).string();
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove(tmp_path, ec);
    }
};

std::string DiskFileSystemTest::tmp_dir_;

TEST_F(DiskFileSystemTest, WriteThenRead) {
    auto dfs = create_disk_filesystem();
    {
        auto r = dfs->open(tmp_path, OpenMode::Write);
        ASSERT_TRUE(r.is_ok());
        auto w = r.value().write_all("disk content");
        ASSERT_TRUE(w.is_ok());
        r.value().close();
    }
    {
        auto r = dfs->open(tmp_path, OpenMode::Read);
        ASSERT_TRUE(r.is_ok());
        auto content = r.value().read_all();
        ASSERT_TRUE(content.is_ok());
        EXPECT_EQ(content.value(), "disk content");
        r.value().close();
    }
}

TEST_F(DiskFileSystemTest, OpenReadMissingReturnsErr) {
    auto dfs = create_disk_filesystem();
    auto r = dfs->open("definitely_nonexistent_file.txt", OpenMode::Read);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::NotFound);
}

TEST_F(DiskFileSystemTest, ExistsChecksFile) {
    auto dfs = create_disk_filesystem();
    {
        auto r = dfs->open(tmp_path, OpenMode::Write);
        ASSERT_TRUE(r.is_ok());
        r.value().write_all("x");
        r.value().close();
    }
    auto e = dfs->exists(tmp_path);
    ASSERT_TRUE(e.is_ok());
    EXPECT_TRUE(e.value());

    auto e2 = dfs->exists(tmp_path + ".missing");
    ASSERT_TRUE(e2.is_ok());
    EXPECT_FALSE(e2.value());
}

TEST_F(DiskFileSystemTest, RemoveDeletesFile) {
    auto dfs = create_disk_filesystem();
    {
        auto r = dfs->open(tmp_path, OpenMode::Write);
        ASSERT_TRUE(r.is_ok());
        r.value().write_all("data");
        r.value().close();
    }
    ASSERT_TRUE(dfs->remove(tmp_path).is_ok());
    auto e = dfs->exists(tmp_path);
    ASSERT_TRUE(e.is_ok());
    EXPECT_FALSE(e.value());
}

}  // namespace
