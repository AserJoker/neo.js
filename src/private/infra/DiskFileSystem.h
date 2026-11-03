// impl_file.h (private) - DiskFileSystem 实现声明
//
// 本头仅在 infra 模块内部可见（src/private/），其他模块无法引用 DiskFileSystem，
// 只能通过 public 的 FileSystem 接口 + create_disk_filesystem() 工厂使用。
// 文件名加 impl_ 前缀，避免与 public/infra/file.h 同名产生 include 歧义。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）

#ifndef NEO_INFRA_PRIVATE_IMPL_FILE_H
#define NEO_INFRA_PRIVATE_IMPL_FILE_H

#include "infra/file.h"  // public 接口（impl_ 前缀消除歧义）

namespace neo::infra {

// 真实实现：包 std::fstream（FFI 调用 + 异常拦截在 .cpp 实现文件内）
class DiskFileSystem : public FileSystem {
public:
    Result<File> open(std::string_view path, OpenMode mode) override;
    Result<void> remove(std::string_view path) override;
    Result<bool> exists(std::string_view path) override;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_FILE_H
