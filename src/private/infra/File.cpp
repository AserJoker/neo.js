// file.cpp - File 方法 + DiskFileSystem 实现 + 工厂函数（FFI 收口：std::fstream）
//
// 本文件是 neo.js 中唯一直接使用 std::fstream 的地方（ADR-027 §5.6）。
// std::fstream 可能抛异常，所有调用 try-catch 兜底转 Result::err（ADR-007）。
//
// 关联 ADR: 006（异常即值）、007（无异常/标准库边界 try-catch）、010（接口-实现-工厂）、
//          027（FFI 封装）、028（infra）

#include "infra/impl_file.h"  // private/infra/impl_file.h

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>

namespace neo::infra {

// ---------------------------------------------------------------------------
// File 方法（Impl 是 File 的公开嵌套类型，定义在 public/file.h）
// ---------------------------------------------------------------------------
File::File() = default;
File::File(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

File::File(File&& o) noexcept = default;
File& File::operator=(File&& o) noexcept = default;
File::~File() = default;

Result<size_t> File::read(std::span<std::byte> buf) {
    if (!impl_) return Result<size_t>::err(ErrCode::InvalidArg);
    try {
        size_t got = 0;
        if (impl_->kind == Impl::Kind::Disk) {
            impl_->fs.read(reinterpret_cast<char*>(buf.data()),
                           static_cast<std::streamsize>(buf.size()));
            got = static_cast<size_t>(impl_->fs.gcount());
        } else {
            impl_->ss.read(reinterpret_cast<char*>(buf.data()),
                           static_cast<std::streamsize>(buf.size()));
            got = static_cast<size_t>(impl_->ss.gcount());
        }
        return Result<size_t>::ok(got);
    } catch (const std::exception&) {
        return Result<size_t>::err(ErrCode::IoError);
    }
}

Result<std::string> File::read_all() {
    if (!impl_) return Result<std::string>::err(ErrCode::InvalidArg);
    try {
        std::string content;
        if (impl_->kind == Impl::Kind::Disk) {
            content.assign(std::istreambuf_iterator<char>(impl_->fs),
                           std::istreambuf_iterator<char>());
        } else {
            content.assign(std::istreambuf_iterator<char>(impl_->ss),
                           std::istreambuf_iterator<char>());
        }
        return Result<std::string>::ok(std::move(content));
    } catch (const std::exception&) {
        return Result<std::string>::err(ErrCode::IoError);
    }
}

Result<size_t> File::write(std::span<const std::byte> buf) {
    if (!impl_) return Result<size_t>::err(ErrCode::InvalidArg);
    try {
        if (impl_->kind == Impl::Kind::Disk) {
            impl_->fs.write(reinterpret_cast<const char*>(buf.data()),
                            static_cast<std::streamsize>(buf.size()));
            if (!impl_->fs) return Result<size_t>::err(ErrCode::IoError);
        } else {
            impl_->ss.write(reinterpret_cast<const char*>(buf.data()),
                            static_cast<std::streamsize>(buf.size()));
        }
        return Result<size_t>::ok(buf.size());
    } catch (const std::exception&) {
        return Result<size_t>::err(ErrCode::IoError);
    }
}

Result<void> File::write_all(std::string_view content) {
    auto r = write(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(content.data()), content.size()));
    if (r.is_err()) return Result<void>::err(r.err());
    if (r.value() != content.size()) return Result<void>::err(ErrCode::IoError);
    return Result<void>::ok();
}

Result<void> File::close() {
    if (!impl_) return Result<void>::ok();  // 幂等
    try {
        if (impl_->kind == Impl::Kind::Disk) {
            if (impl_->fs.is_open()) impl_->fs.close();
        } else if (impl_->on_close) {
            // memory 模式：调用 FileSystem 实现注入的回写闭包
            impl_->ss.flush();
            impl_->on_close(impl_->ss.str());
        }
        impl_.reset();
        return Result<void>::ok();
    } catch (const std::exception&) {
        return Result<void>::err(ErrCode::IoError);
    }
}

// ---------------------------------------------------------------------------
// DiskFileSystem — 包 std::fstream，异常拦截
// ---------------------------------------------------------------------------
static std::ios::openmode to_openmode(OpenMode mode) {
    switch (mode) {
        case OpenMode::Read:      return std::ios::in | std::ios::binary;
        case OpenMode::Write:     return std::ios::out | std::ios::trunc | std::ios::binary;
        case OpenMode::ReadWrite: return std::ios::in | std::ios::out | std::ios::binary;
        case OpenMode::Append:    return std::ios::out | std::ios::app | std::ios::binary;
    }
    return std::ios::in | std::ios::binary;
}

Result<File> DiskFileSystem::open(std::string_view path, OpenMode mode) {
    try {
        auto impl = std::make_unique<File::Impl>();
        impl->kind = File::Impl::Kind::Disk;
        impl->fs.open(std::string(path), to_openmode(mode));
        if (!impl->fs.is_open()) {
            return Result<File>::err(ErrCode::NotFound);
        }
        return Result<File>::ok(File::make(std::move(impl)));
    } catch (const std::exception&) {
        return Result<File>::err(ErrCode::IoError);
    }
}

Result<void> DiskFileSystem::remove(std::string_view path) {
    try {
        if (std::remove(std::string(path).c_str()) != 0) {
            return Result<void>::err(ErrCode::NotFound);
        }
        return Result<void>::ok();
    } catch (const std::exception&) {
        return Result<void>::err(ErrCode::IoError);
    }
}

Result<bool> DiskFileSystem::exists(std::string_view path) {
    try {
        std::string p(path);
        std::ifstream f(p);
        return Result<bool>::ok(f.good());
    } catch (const std::exception&) {
        return Result<bool>::err(ErrCode::IoError);
    }
}

// 工厂函数：创建 DiskFileSystem，返回接口指针
std::unique_ptr<FileSystem> create_disk_filesystem() {
    return std::make_unique<DiskFileSystem>();
}

}  // namespace neo::infra
