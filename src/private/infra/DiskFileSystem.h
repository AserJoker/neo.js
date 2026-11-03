// DiskFileSystem.h (private) - DiskFileSystem 实现声明
//
// 本头仅在 infra 模块内部可见（src/private/），其他模块无法引用 DiskFileSystem，
// 只能通过 public 的 IFileSystem 接口 + create_disk_filesystem() 工厂使用。
// 文件名与实现类名一致（DiskFileSystem.h），File.h 是具体类头（含 File+OpenMode）。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）

#ifndef NEO_INFRA_PRIVATE_DISK_FILE_SYSTEM_H
#define NEO_INFRA_PRIVATE_DISK_FILE_SYSTEM_H

#include "infra/IFileSystem.h"  // public 接口

namespace neo::infra {

// 真实实现：包 std::fstream（FFI 调用 + 异常拦截在 .cpp 实现文件内）
class DiskFileSystem : public IFileSystem {
public:
    Result<File> open(std::string_view path, OpenMode mode) override;
    Result<void> remove(std::string_view path) override;
    Result<bool> exists(std::string_view path) override;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_DISK_FILE_SYSTEM_H
