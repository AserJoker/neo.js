// IFileSystem.h - 文件系统接口（FFI 收口：文件 I/O）
//
// 业务代码持 IFileSystem*，不直接调 std::ifstream/fopen（ADR-027 §5.6 FFI 封装）。
// 具体实现（NativeFileSystem）在 private，通过工厂 createNativeFileSystem() 获取。
// 测试可注入 mock IFileSystem 验证读取失败路径，无需真实文件。
//
// 本阶段只提供只读整文件读取：源码/eval 字符串一次性读入内存，再交给字符流。
// 读取按二进制进行，不做换行或编码转换（CRLF 原样保留，由前端字符流解释）。
//
// 关联 ADR: 006（异常即值）、010（接口-实现-工厂）、027（FFI 封装）、028（infra）、
//          052（前端源流）

#ifndef NEO_INFRA_IFILESYSTEM_H
#define NEO_INFRA_IFILESYSTEM_H

#include "infra/Result.h"

#include <memory>
#include <string>
#include <string_view>

namespace neo::infra {

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    // 一次性读取整个文件。
    // path 为 UTF-8 编码；成功返回完整字节内容（可能为空），
    // 失败返回 ErrCode::NotFound（不存在）或 ErrCode::IoError（打开/读取失败）。
    virtual Result<std::string> readFile(std::string_view path) = 0;
};

// 工厂：创建基于宿主文件系统的实现。
std::unique_ptr<IFileSystem> createNativeFileSystem();

}  // namespace neo::infra

#endif  // NEO_INFRA_IFILESYSTEM_H
