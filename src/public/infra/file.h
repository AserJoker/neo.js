// file.h - 文件封装（FFI 收口：std::fstream）
//
// FileSystem 抽象接口，DiskFileSystem 包 std::fstream（FFI 收口+异常拦截在 .cpp），
// MemoryFileSystem 测试用（内存虚拟文件）。File 操作返回 Result<T>，IO 错误转 err。
//
// 关联 ADR: 006（异常即值）、007（标准库边界 try-catch）、027（FFI 封装）、028（infra）

#ifndef NEO_INFRA_FILE_H
#define NEO_INFRA_FILE_H

#include "infra/result.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace neo::infra {

// 文件打开模式
enum class OpenMode {
    Read,           // 只读，文件必须存在
    Write,          // 只写，截断/创建
    ReadWrite,      // 读写，文件必须存在
    Append,         // 追加写，创建
};

// 文件句柄：封装底层流（真实 fstream 或内存 stringstream）
class File {
public:
    File();
    ~File();

    File(File&& o) noexcept;
    File& operator=(File&& o) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    // 读 buf.size() 字节到 buf，返回实际读取字节数（EOF 时 < buf.size()）
    Result<size_t> read(std::span<std::byte> buf);
    Result<std::string> read_all();

    // 写 buf.size() 字节，返回实际写入字节数
    Result<size_t> write(std::span<const std::byte> buf);
    Result<void>   write_all(std::string_view content);

    Result<void> close();

    bool is_open() const { return impl_ != nullptr; }

private:
    friend class DiskFileSystem;
    friend class MemoryFileSystem;
    struct Impl;
    explicit File(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// 文件系统抽象
class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual Result<File>   open(std::string_view path, OpenMode mode) = 0;
    virtual Result<void>   remove(std::string_view path) = 0;
    virtual Result<bool>   exists(std::string_view path) = 0;
};

// 真实实现：包 std::fstream（FFI 收口 + 异常拦截在 .cpp）
class DiskFileSystem : public FileSystem {
public:
    Result<File> open(std::string_view path, OpenMode mode) override;
    Result<void> remove(std::string_view path) override;
    Result<bool> exists(std::string_view path) override;
};

// 测试用：内存虚拟文件系统
class MemoryFileSystem : public FileSystem {
public:
    // 预置一个文件内容（测试 setup 用）
    void add_file(std::string_view path, std::string content);

    Result<File> open(std::string_view path, OpenMode mode) override;
    Result<void> remove(std::string_view path) override;
    Result<bool> exists(std::string_view path) override;

    // 取出文件当前内容（测试断言用）
    std::string get_content(std::string_view path) const;

private:
    struct Entry {
        std::string content;
        bool        exists = true;
    };
    std::unordered_map<std::string, Entry> files_;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_FILE_H
