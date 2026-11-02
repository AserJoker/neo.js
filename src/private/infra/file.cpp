// file.cpp - File/DiskFileSystem/MemoryFileSystem 实现
//
// 本文件是 neo.js 中唯一直接使用 std::fstream 的地方（ADR-027 §5.6）。
// std::fstream 可能抛异常，所有调用 try-catch 兜底转 Result::err（ADR-007）。
//
// 关联 ADR: 006（异常即值）、007（无异常/标准库边界 try-catch）、027（FFI 封装）、028（infra）

#include "infra/file.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace neo::infra {

// ---------------------------------------------------------------------------
// File::Impl — 持有底层流，两种实现：磁盘 fstream / 内存 stringstream
// ---------------------------------------------------------------------------
struct File::Impl {
    enum class Kind { Disk, Memory };
    Kind kind = Kind::Disk;
    // disk 用 fs；memory 用 ss（fstream 默认构造，不打开）
    std::fstream      fs;
    std::stringstream ss;
    bool              owns_memory_entry = false;  // memory 模式：析构时回写内容
    MemoryFileSystem* mem_fs            = nullptr;
    std::string       mem_path;                  // memory 模式：回写路径
};

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
            // 用 streambuf 迭代器读取全部
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
        } else if (impl_->owns_memory_entry && impl_->mem_fs) {
            // 回写内存文件系统
            impl_->ss.flush();
            impl_->mem_fs->add_file(impl_->mem_path, impl_->ss.str());
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
        return Result<File>::ok(File(std::move(impl)));
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

// ---------------------------------------------------------------------------
// MemoryFileSystem — 内存虚拟文件，无磁盘 I/O
// ---------------------------------------------------------------------------
void MemoryFileSystem::add_file(std::string_view path, std::string content) {
    files_[std::string(path)] = {std::move(content), true};
}

Result<File> MemoryFileSystem::open(std::string_view path, OpenMode mode) {
    std::string key(path);
    auto it = files_.find(key);
    bool exists = (it != files_.end() && it->second.exists);

    if (mode == OpenMode::Read || mode == OpenMode::ReadWrite) {
        if (!exists) return Result<File>::err(ErrCode::NotFound);
    }

    auto impl = std::make_unique<File::Impl>();
    impl->kind = File::Impl::Kind::Memory;
    impl->mem_fs = this;
    impl->mem_path = key;
    impl->owns_memory_entry = (mode == OpenMode::Write || mode == OpenMode::Append);

    if (exists && (mode == OpenMode::Read || mode == OpenMode::ReadWrite || mode == OpenMode::Append)) {
        impl->ss.str(it->second.content);
    }
    // Write 模式：清空（ss 默认空）
    // Read 模式：从内容读
    if (mode == OpenMode::Read) {
        // 只读：不回写
        impl->owns_memory_entry = false;
    } else if (mode == OpenMode::Append) {
        // 追加：写指针移到末尾
        impl->ss.clear();
        impl->ss.seekp(0, std::ios::end);
    }

    return Result<File>::ok(File(std::move(impl)));
}

Result<void> MemoryFileSystem::remove(std::string_view path) {
    auto it = files_.find(std::string(path));
    if (it == files_.end() || !it->second.exists) {
        return Result<void>::err(ErrCode::NotFound);
    }
    it->second.exists = false;
    return Result<void>::ok();
}

Result<bool> MemoryFileSystem::exists(std::string_view path) {
    auto it = files_.find(std::string(path));
    return Result<bool>::ok(it != files_.end() && it->second.exists);
}

std::string MemoryFileSystem::get_content(std::string_view path) const {
    auto it = files_.find(std::string(path));
    if (it == files_.end() || !it->second.exists) return "";
    return it->second.content;
}

}  // namespace neo::infra
