// file.h - 文件封装接口（FFI 收口：std::fstream）
//
// FileSystem 抽象接口，File 操作返回 Result<T>，IO 错误转 err。
// 具体实现 DiskFileSystem 在 private，通过工厂 create_disk_filesystem() 获取。
// 测试用 MemoryFileSystem（在 tests/unit/infra/）注入验证。
//
// 关联 ADR: 006（异常即值）、007（标准库边界 try-catch）、010（接口-实现-工厂）、
//          027（FFI 封装）、028（infra）

#ifndef NEO_INFRA_FILE_H
#define NEO_INFRA_FILE_H

#include "infra/result.h"

#include <fstream>
#include <functional>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace neo::infra {

// 前向声明（File::Impl 引用 FileSystem*）
class FileSystem;

// 文件打开模式
enum class OpenMode {
    Read,           // 只读，文件必须存在
    Write,          // 只写，截断/创建
    ReadWrite,      // 读写，文件必须存在
    Append,         // 追加写，创建
};

// 文件句柄：封装底层流（真实 fstream 或内存 stringstream）
//
// Impl 是公开嵌套类型（仅数据成员，无业务方法），供 FileSystem 实现构造 File。
// 真实 DiskFileSystem 和测试 MemoryFileSystem 都通过 File::make(std::make_unique<Impl>(...))
// 构造 File，无需 friend 关系。
class File {
public:
    // 文件实现数据：FileSystem 实现填充并交给 File 管理
    struct Impl {
        enum class Kind { Disk, Memory };
        Kind kind = Kind::Disk;
        // disk 用 fs；memory 用 ss
        // 注：fstream/stringstream 在此公开，但仅 FileSystem 实现应访问
        // （DiskFileSystem 在 private，MemoryFileSystem 在 tests）
        std::fstream      fs;
        std::stringstream ss;
        // memory 模式：close 时回写内容的回调（由 MemoryFileSystem 在 open 时注入）。
        // 闭包捕获 ss 引用与 fs 路径，把 ss.str() 写回 fs->files_。
        // DiskFileSystem 不设置此回调。这样 close() 无需知道 MemoryFileSystem 类型，
        // 避免在 public 接口暴露回写协议，也无需 friend 跨编译单元。
        std::function<void(std::string_view content)> on_close;
    };

    File();
    ~File();

    File(File&& o) noexcept;
    File& operator=(File&& o) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    // 工厂：FileSystem 实现用它构造 File（接管 impl 所有权）
    static File make(std::unique_ptr<Impl> impl) { return File(std::move(impl)); }

    // 读 buf.size() 字节到 buf，返回实际读取字节数（EOF 时 < buf.size()）
    Result<size_t> read(std::span<std::byte> buf);
    Result<std::string> read_all();

    // 写 buf.size() 字节，返回实际写入字节数
    Result<size_t> write(std::span<const std::byte> buf);
    Result<void>   write_all(std::string_view content);

    Result<void> close();

    bool is_open() const { return impl_ != nullptr; }

private:
    std::unique_ptr<Impl> impl_;
    explicit File(std::unique_ptr<Impl> impl);
};

// 文件系统抽象接口
class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual Result<File>   open(std::string_view path, OpenMode mode) = 0;
    virtual Result<void>   remove(std::string_view path) = 0;
    virtual Result<bool>   exists(std::string_view path) = 0;
};

// 工厂：创建真实磁盘文件系统（具体实现 DiskFileSystem 在 private）
std::unique_ptr<FileSystem> create_disk_filesystem();

}  // namespace neo::infra

#endif  // NEO_INFRA_FILE_H
