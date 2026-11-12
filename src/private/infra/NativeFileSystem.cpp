// NativeFileSystem.cpp - 宿主文件系统实现（FFI 收口）
//
// 用 std::filesystem 处理 UTF-8 路径（Windows 下内部转宽字符），
// std::ifstream 以二进制模式分块读取。错误统一映射为 ErrCode。
//
// 关联 ADR: 006（异常即值）、027（FFI 封装）、028（infra）、052（前端源流）

#include "infra/IFileSystem.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace neo::infra {

namespace {

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    // C++20：path 接受 u8string，Windows 下按 UTF-8 → UTF-16 转换。
    std::u8string text(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return std::filesystem::path(text);
}

class NativeFileSystem final : public IFileSystem {
public:
    Result<std::string> readFile(std::string_view path) override {
        if (path.empty()) return Result<std::string>::err(ErrCode::InvalidArg);

        const std::filesystem::path nativePath = pathFromUtf8(path);
        std::error_code ec;
        const bool exists = std::filesystem::exists(nativePath, ec);
        if (ec) return Result<std::string>::err(ErrCode::IoError);
        if (!exists) return Result<std::string>::err(ErrCode::NotFound);

        std::ifstream in(nativePath, std::ios::binary);
        if (!in) return Result<std::string>::err(ErrCode::IoError);

        std::string data;
        char buffer[64 * 1024];
        while (in) {
            in.read(buffer, static_cast<std::streamsize>(sizeof(buffer)));
            const std::streamsize got = in.gcount();
            if (got > 0) {
                data.append(buffer, static_cast<size_t>(got));
            }
        }
        // 正常结束是 eofbit；其余（badbit/failbit 非 eof）视为读取失败。
        if (!in.eof()) return Result<std::string>::err(ErrCode::IoError);
        return Result<std::string>::ok(std::move(data));
    }
};

}  // namespace

std::unique_ptr<IFileSystem> createNativeFileSystem() {
    return std::make_unique<NativeFileSystem>();
}

}  // namespace neo::infra
