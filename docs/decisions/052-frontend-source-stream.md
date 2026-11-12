# ADR-052: 前端源流 — 文件 API 与 UTF-8 字符流

- 状态: 已接受 (2026-11-12)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-006（异常即值）、ADR-010（接口-实现-工厂）、ADR-027（FFI 封装）、
          ADR-028（infra）、ADR-047（SourceText/SourceSlice）

## 背景

前端（词法/语法）需要最底层的源文本输入。约束：

1. 不使用 `std::stringstream`——词法器需要可预测的步进，并在前进 `pos` 的同时
   同步 `line/column`；
2. 字符流只读，数据源是**一次性读入内存的完整源码**；
3. 文件读取与 `eval` / `new Function` 的字符串必须走同一条字符流路径；
4. 文件 I/O 是 FFI，按 ADR-027 收口到 infra 接口，便于测试注入。

## 决策

### 1. 文件 API：`IFileSystem`（只读整文件）

```cpp
class IFileSystem {
public:
    virtual Result<std::string> readFile(std::string_view path) = 0;
};
std::unique_ptr<IFileSystem> createNativeFileSystem();
```

- 路径为 UTF-8；`NativeFileSystem` 用 `std::filesystem::path(u8string)` 转换，
  Windows 下由标准库转宽字符，避免 ANSI 代码页丢字。
- 以二进制模式分块读取，**不做换行/编码转换**：CRLF 原样保留，由字符流解释。
- 错误映射：空路径 → `InvalidArg`；不存在 → `NotFound`；打开/读取失败 → `IoError`。
- 测试可注入 mock `IFileSystem`，不需要真实文件。

### 2. 字符流：`CharStream`（UTF-8 码点）

数据源是 `std::string_view`，**流不拥有源码**；调用方保证缓冲在流生命周期内有效。
文件读取结果、eval/new Function 字符串都只是内存缓冲，接口完全一致。

- `offset`：字节偏移，用于源码切片（`SourceSlice`）。
- `line` / `column`：1-based **码点**位置，用于诊断。
- `peek(lookahead)`：不改变状态，越过末尾返回 `kEndOfInput`。
- `advance()`：消费一个码点并同步位置；末尾返回 `kEndOfInput` 且不改变状态。
- 行终止符：LF、CR、CRLF（CRLF 只记一行）、U+2028、U+2029。
- 多字节码点列号 +1（不是按字节）。
- 非法 UTF-8：按 U+FFFD 消费一个字节，并置 `hadInvalidUtf8()`；词法器据此报
  SyntaxError，而不是让解码失败静默吞掉字节。

### 3. 为什么按码点而不是按字节

- JS 的标识符、字符串、行终止符都按 Unicode 码点/UTF-16 语义；按字节步进会让
  `column` 与用户看到的列不一致。
- 引擎字符串已经是 UTF-8，`unicode/Utf8.h` 已有 `decodeCodePoint`，直接复用。
- 词法器仍可用 `peek() == 'a'` 做 ASCII 快速判断；多字节只在标识符/字符串路径出现。

### 4. 生命周期与后续接入

- 文件读取：`IFileSystem::readFile` 返回 `std::string`，调用方持有它并构造
  `CharStream(std::string_view)`。
- 模块：后续把源码移入 `core::SourceText`（ADR-047），字符流仍取
  `SourceText::text` 的 view，函数只保存 `SourceSlice`，不复制源码。
- eval/new Function：直接把参数字符串作为缓冲构造字符流，无需临时文件或
  stringstream。

## 后果

- 优点：文件与字符串输入同构；位置信息（offset/line/column）在流层一次算对；
  FFI 收口后文件读取可 mock；无 `std::stringstream` 的格式化开销与隐式状态。
- 缺点：`CharStream` 不拥有源码，调用方必须保证缓冲生命周期；`peek(k)` 每次从
  当前偏移重新解码，k 较大时为 O(k)（词法器只用 0/1，可接受）。
- 未覆盖：BOM 处理、源文件编码检测（按 UTF-8）、Unicode 标识符分类（后续词法器）。
- 测试：`tests/unit/infra/FileSystemTest.cpp`（4 例）、
  `tests/unit/frontend/CharStreamTest.cpp`（9 例）；全量回归通过。
