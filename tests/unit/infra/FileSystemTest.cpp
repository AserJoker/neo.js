// FileSystemTest.cpp - IFileSystem 整文件读取
//
// 覆盖：二进制内容（含 NUL/CRLF/UTF-8）原样读取、空文件、不存在、空路径。

#include "infra/IFileSystem.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using neo::infra::ErrCode;
using neo::infra::IFileSystem;
using neo::infra::createNativeFileSystem;

std::string toUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path uniqueTempPath(const char* tag) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string("neo_") + tag + "_" + std::to_string(stamp));
}

struct TempFile {
    std::filesystem::path path;
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

void writeBytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
}

}  // namespace

TEST(FileSystemTest, ReadsBinaryContentVerbatim) {
    TempFile file{uniqueTempPath("fs_binary")};
    std::string bytes = std::string("a\0b\r\n", 5) + "\xE4\xB8\xAD";
    writeBytes(file.path, bytes);

    auto fs = createNativeFileSystem();
    auto result = fs->readFile(toUtf8(file.path));
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), bytes);
}

TEST(FileSystemTest, ReadsEmptyFile) {
    TempFile file{uniqueTempPath("fs_empty")};
    writeBytes(file.path, "");

    auto fs = createNativeFileSystem();
    auto result = fs->readFile(toUtf8(file.path));
    ASSERT_TRUE(result.isOk());
    EXPECT_TRUE(result.value().empty());
}

TEST(FileSystemTest, MissingFileIsNotFound) {
    auto fs = createNativeFileSystem();
    auto result = fs->readFile(toUtf8(uniqueTempPath("fs_missing")));
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.err(), ErrCode::NotFound);
}

TEST(FileSystemTest, EmptyPathIsInvalidArgument) {
    auto fs = createNativeFileSystem();
    auto result = fs->readFile("");
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.err(), ErrCode::InvalidArg);
}
