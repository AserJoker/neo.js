# ADR-028: M1 基础设施设计（infra 模块）

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-007（开发规范：异常策略/组合>继承/依赖注入）、ADR-008（Allocator）、ADR-009（源码结构）、ADR-027（可测试性/FFI 封装）

## 背景

M1 实现业务无关基础设施，为后续 M2+ 语言核心模块提供可测试的底层工具。
这些工具不依赖任何语言核心（Value/Scope/GC 等），纯 C++ 基础设施。
核心约束：FFI 收口（ADR-027 §5.6）——`malloc`/`free`/`std::cout`/`std::fstream`
等不可 mock 的 FFI 必须封装成接口对象后注入使用。

## 决策

### 1. 新增 infra 模块（业务无关基础设施）

- 命名空间：`neo::infra`
- 目录：`src/public/infra/`（接口）+ `src/private/infra/`（实现）
- 测试：`tests/unit/infra/`
- 依赖方向：infra 不依赖任何其他 neo 模块；其他模块可依赖 infra

infra 模块含 5 个组件：`Result<T>` / `Allocator` / `Console` / `Command` / `File`。

### 2. Result<T> — 统一错误返回

C++ 内部不抛异常（ADR-007），FFI 边界 catch C++ 异常转 `Result::err`（ADR-006 异常即值）。

```cpp
// src/public/infra/Result.h
namespace neo::infra {

enum class ErrCode : int {
    Ok = 0,
    InvalidArg,
    OutOfRange,
    IoError,
    NotFound,
    AlreadyExists,
    // ...
};

template <typename T>
class Result {
    bool ok_;
    union {
        T value_;
        ErrCode err_;
    };
public:
    static Result ok(T v);
    static Result err(ErrCode e);
    bool is_ok() const;
    bool is_err() const;
    T&       value();      // is_err() 时 UB（编程错误）
    const T& value() const;
    ErrCode  err() const;
};

}  // namespace neo::infra
```

- 二元 union（值或错误码），无堆分配。
- `value()` 在 `is_err()` 时是**编程错误**（UB），不抛异常——调用方应先 `is_ok()` 判断。
- `Result<void>` 特化：只有 ok_/err_，无 value_。
- 辅助函数 `try_capture()`：包 FFI 调用，catch 标准库异常转 `Result::err`。

### 3. Allocator — 内存分配收口（ADR-008 落地）

```cpp
// src/public/infra/IAllocator.h
namespace neo::infra {

class Allocator {
public:
    virtual ~Allocator() = default;
    virtual void* alloc(size_t bytes) = 0;
    virtual void* alloc_aligned(size_t bytes, size_t align) = 0;
    virtual void  free(void* p) = 0;
    virtual void  free_aligned(void* p) = 0;

    // 内存配额管理（ADR-028 §3 补充）
    // 限额默认 size_t 最大值（无限制）；可在运行时收紧。
    // 超限 alloc 返回 nullptr（拦截），不抛异常、不耗尽 OS 资源。
    // 解释器/VM 出现无限分配 bug 时由限额兜底，配合上层错误处理转为脚本异常。
    virtual void  set_limit(size_t bytes) = 0;
    virtual size_t limit() const = 0;
    virtual size_t used() const = 0;
};

// 工厂：默认无限额；带限额版本用于受控执行环境
std::unique_ptr<Allocator> create_malloc_allocator();
std::unique_ptr<Allocator> create_malloc_allocator_with_limit(size_t bytes);

}  // namespace neo::infra
```

- **业务代码持 `Allocator*`，不直接调 malloc/free**（ADR-027 §5.6）。
- `MallocAllocator` 实现在 `src/private/infra/allocator.cpp`，FFI 调用集中在该文件。
- **内存配额**（2026-11 补充）：`MallocAllocator` 用 `std::atomic<size_t>` 记账已用字节，
  `alloc` 前原子地 CAS 检查 `used + bytes <= limit`，超限返回 nullptr；`free` 查记账表
  （`std::unordered_map<void*, size_t>`）递减 used。线程安全，适合 VM/GC 多线程场景。
  默认 `limit() = SIZE_MAX`（无限制），向后兼容；`create_malloc_allocator_with_limit(n)`
  创建带限额实例。无限分配 bug 在限额处被拦截，配合 `Result<T>` 错误通道转为脚本异常，
  避免 OOM 拖垮宿主进程。
- 测试：注入 mock Allocator 验证分配/释放次数、参数、可模拟分配失败。

### 4. Console — 输出工具

```cpp
// src/public/infra/IOutputSink.h
namespace neo::infra {

class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual void write(std::string_view text) = 0;
    virtual void write_line(std::string_view text) = 0;
    virtual void flush() = 0;
};

// 真实实现：包 std::cout（FFI 收口）
class StdoutSink : public OutputSink { ... };

class Console {
    OutputSink* sink_;  // 构造注入，nullptr 用默认 StdoutSink
public:
    explicit Console(OutputSink* sink = nullptr);

    template <typename... Args>
    void print(std::format_string<Args...> fmt, Args&&... args);

    template <typename... Args>
    void print_line(std::format_string<Args...> fmt, Args&&... args);
};

}  // namespace neo::infra
```

- **`OutputSink` 抽象**：真实 `StdoutSink` 包 `std::cout`，测试注入 `StringSink`（写 std::string）截获输出。
- **`std::format` 泛型**：双平台 clang 已验证可用。
- `Console` 持 `OutputSink*`（不持有所有权，由调用方管理生命周期）。

### 5. Command — 命令行参数解析

```cpp
// src/public/infra/CommandParser.h
namespace neo::infra {

struct CommandOption {
    std::string name;        // --name
    std::string short_name;  // -n（可空）
    bool        takes_value; // 是否带值（--name=value）
    std::string description;
};

class CommandParser {
    std::vector<CommandOption> options_;
public:
    void add_option(CommandOption opt);

    // 解析 argv，返回 ParsedArgs 或 ErrCode
    Result<ParsedArgs> parse(int argc, const char* const argv[]);
};

struct ParsedArgs {
    std::unordered_map<std::string, std::string> options;
    std::vector<std::string>                      positionals;
    bool has(const std::string& name) const;
};

}  // namespace neo::infra
```

- **纯逻辑，无 FFI**——`argv` 由调用方传入，最易测试。
- 支持 `--flag`、`--opt=value`、`-o value`、positional。
- 返回 `Result<ParsedArgs>`，参数错误返回 `ErrCode::InvalidArg`。

### 6. File — 文件封装（FileSystem 接口注入）

```cpp
// src/public/infra/IFileSystem.h
namespace neo::infra {

class File {
    // 持有底层流（真实 std::fstream 或内存 std::stringstream）
public:
    Result<size_t> read(std::span<std::byte> buf);
    Result<std::string> read_all();
    Result<size_t> write(std::span<const std::byte> buf);
    Result<void>   write_all(std::string_view content);
    Result<void>   close();
};

class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual Result<File>   open(std::string_view path, std::string_view mode) = 0;
    virtual Result<void>   remove(std::string_view path) = 0;
    virtual Result<bool>   exists(std::string_view path) = 0;
};

// 真实实现：包 std::fstream（FFI 收口 + 异常拦截）
class DiskFileSystem : public FileSystem { ... };

// 测试用：内存虚拟文件系统
class MemoryFileSystem : public FileSystem { ... };

}  // namespace neo::infra
```

- **方案 A：FileSystem 接口注入**。`File` 具体类，底层流由 `FileSystem` 提供。
- `DiskFileSystem` 包 `std::fstream`，FFI 调用 + 异常拦截集中在实现文件内，
  `try-catch` 兜底转 `Result::err`（ADR-007）。
- `MemoryFileSystem` 测试用：内存 `std::stringstream`，无磁盘 I/O，可注入预置文件内容。
- 所有操作返回 `Result<T>`，IO 错误返回 `ErrCode::IoError`。

### 7. 可测试性落实（ADR-027 §5.6）

| 组件 | FFI 收口对象 | Mock 注入点 |
|------|------------|------------|
| Allocator | `MallocAllocator`（包 malloc/free） | `Allocator*` 接口 |
| Console | `StdoutSink`（包 std::cout） | `OutputSink*` 接口 |
| File | `DiskFileSystem`（包 std::fstream） | `FileSystem*` 接口 |
| Command | 无 FFI | 直接传 argv |
| Result | 无 FFI | 纯类型 |

- 业务代码只见接口，不见 FFI。
- FFI 调用集中在 `src/private/infra/*.cpp` 实现文件内。
- 每个 FFI 封装对象有对应 mock 实现（测试用，放 `tests/unit/infra/`）。

### 8. 实现顺序

按依赖关系自底向上：
1. **Result<T>** — 无依赖，被其他组件复用
2. **Allocator** — 依赖 Result（分配失败返回 err）
3. **Console** — 依赖 Result（写入失败返回 err？或 void，待定）
4. **Command** — 依赖 Result
5. **File** — 依赖 Result

## 后果

- 优点：FFI 全部收口，业务代码可测试；统一 Result 错误通道；infra 不依赖语言核心，可独立验证。
- 缺点：Result 二元 union 增加 API 复杂度（每次调用要判断 is_ok）；mock 实现需维护。
- 后续：M2+ 语言核心模块基于 infra 构建（GC 用 Allocator，stdlib 用 Console/File）。
