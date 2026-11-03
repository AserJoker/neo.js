// IFileSystem.h - 文件系统抽象接口（FFI 收口：std::fstream）
//
// IFileSystem 抽象接口，File 操作返回 Result<T>，IO 错误转 err。
// 具体实现 DiskFileSystem 在 private，通过工厂 create_disk_filesystem() 获取。
// 测试用 MemoryFileSystem（在 tests/unit/infra/）注入验证。
//
// 关联 ADR: 006（异常即值）、007（标准库边界 try-catch）、010（接口-实现-工厂）、
//          027（FFI 封装）、028（infra）

#ifndef NEO_INFRA_IFILESYSTEM_H
#define NEO_INFRA_IFILESYSTEM_H

#include "infra/File.h"
#include "infra/Result.h"

#include <memory>
#include <string_view>

namespace neo::infra {

// 文件系统抽象接口
class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual Result<File>   open(std::string_view path, OpenMode mode) = 0;
    virtual Result<void>   remove(std::string_view path) = 0;
    virtual Result<bool>   exists(std::string_view path) = 0;
};

// 工厂：创建真实磁盘文件系统（具体实现 DiskFileSystem 在 private）
std::unique_ptr<IFileSystem> create_disk_filesystem();

}  // namespace neo::infra

#endif  // NEO_INFRA_IFILESYSTEM_H
