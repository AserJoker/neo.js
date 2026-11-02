# ADR-026: M0 项目搭建与基础设施

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-003（Unicode/ICU）、ADR-007（开发规范）、ADR-008（内存分层）、ADR-009（源码结构）

## 背景

确定 M0 阶段（项目搭建与基础设施）的范围与技术选型：构建系统、三方件引入策略、
目录骨架、配置文件。目标：先跑起来 HelloWorld，验证构建链路通畅，为 M1 做准备。

## 决策

### 1. 构建系统：CMake + Ninja + Clang，双平台同步

- **CMake** 作为构建系统元信息工具。
- **Ninja** 作为底层构建工具（比 Make 更快，CMake 原生支持）。
- **Clang** 作为唯一编译器，**双平台（Windows + WSL/Linux）同步**。
- **Windows 不使用 MSVC**（`clang-cl` 也不用），统一用 MinGW/LLVM 的 clang，
  目的：**安全使用 clang 扩展**，双平台编译器行为一致，避免 MSVC 与 clang 的
  语义差异（如 `__builtin_*`、表达式求值顺序、模板实例化细节）。
- **C++20 标准**（`-std=c++20`），允许使用 clang 扩展。
- 最低编译器版本：clang 14+（C++20 完整支持）。

### 2. 三方件引入：混合策略

| 依赖 | 引入方式 | 位置 | M0 启用 |
|------|---------|------|---------|
| **GoogleTest** | git submodule | `third_party/googletest/` | ✅ 默认启用 |
| **ICU** | FetchContent（本地 port） | `third_party/icu/`（port 代码）+ `third_party/icu/download/`（预下载缓存） | ❌ 默认关闭（`NEO_ENABLE_ICU` option） |

#### GTest — submodule

- 通过 `git submodule add` 引入到 `third_party/googletest/`。
- CMake 用 `add_subdirectory(third_party/googletest)` 集成。
- git 协议不受限，可正常 clone。

#### ICU — FetchContent（本地 port）

- `D:\projects\icu` 是一个 **CMake port**（封装好的 FetchContent 入口），
  内部支持本地 zip / 网络下载两种源。
- M0 将该 port 复制到 `third_party/icu/`。
- **离线兜底**：`third_party/icu/download/` 目录用于存放预下载的 ICU 源码/数据包
  （icu4c-*-src.tgz、icu4c-*-data-bin-l.zip、icudt*.dat），port 自动检测使用。
  网络受限环境下开发者手工放入，无需改 CMake 变量。
- `third_party/icu/download/` 加入 `.gitignore`（预下载包不入库）。
- **M0 默认不启用 ICU**（`NEO_ENABLE_ICU=OFF`），因 ICU 编译重且 HelloWorld 不需要。
  M1 起 Unicode 子系统接入时开启。

### 3. 目录骨架（按 ADR-009 三层头文件）

```
neo.js/
├── CMakeLists.txt                 # 顶层构建脚本
├── CMakePresets.json              # 构建预设（Clang + Ninja 双平台）
├── .gitignore
├── .gitattributes                 # LF + UTF-8 无 BOM
├── .clang-format                  # 代码格式化
├── .editorconfig                  # 编辑器统一配置
├── README.md
├── docs/                          # 设计文档（已存在）
│   ├── design/
│   ├── decisions/                 # ADR
│   ├── spec/
│   ├── HANDOFF.md
│   └── plan.md
├── include/                       # ① 唯一对外 C API
│   └── neojs.h
├── src/
│   ├── public/                    # ② 模块间公共头
│   │   ├── core/
│   │   ├── gc/
│   │   ├── vm/
│   │   ├── frontend/
│   │   ├── sema/
│   │   ├── compiler/
│   │   ├── stdlib/
│   │   ├── unicode/
│   │   ├── api/
│   │   └── debug/
│   └── private/                   # ③ 内部实现头/源
│       ├── core/
│       │   └── hello.cpp          # M0 HelloWorld 实现
│       ├── gc/
│       ├── vm/
│       ├── frontend/
│       ├── sema/
│       ├── compiler/
│       ├── stdlib/
│       ├── unicode/
│       ├── api/
│       └── debug/
├── tests/
│   └── hello_test.cpp             # M0 GTest 验证用例
└── third_party/
    ├── googletest/                # submodule
    └── icu/                       # FetchContent port（M0 占位，默认不启用）
        ├── CMakeLists.txt
        ├── icu_data.c
        ├── icu_data.h
        ├── bin_to_c.py
        ├── bin_to_c.ps1
        └── download/              # 预下载缓存（gitignore）
            └── .gitkeep
```

- M0 只在 `src/private/core/` 放 HelloWorld 实现，其余模块目录建空占位
  （.gitkeep），等 M1+ 填充。
- `include/neojs.h` 放最小 C API 占位（空函数），M1 起充实。

### 4. 配置文件清单

| 文件 | 作用 | 关键配置 |
|------|------|---------|
| `.gitattributes` | 跨平台换行/编码 | `* text=auto eol=lf`，保证 LF + UTF-8 无 BOM |
| `.gitignore` | 忽略构建产物/缓存 | build/、.cache/、third_party/icu/download/*（保留 .gitkeep）等 |
| `.clang-format` | 代码格式化 | 基于 LLVM 风格调整，CI/编辑器集成 |
| `.editorconfig` | 编辑器统一 | 缩进/换行/编码，与 .clang-format/.gitattributes 一致 |
| `CMakePresets.json` | 构建预设 | Windows/WSL 两个 preset，统一 Clang+Ninja + C++20 + -Werror |

### 5. 编译告警（落实 ADR-007）

- `-Wall -Wextra -Werror`（警告即错误）。
- `-Wpedantic`（严格标准）。
- 额外 clang 诊断：`-Wshadow -Wconversion -Wnull-dereference` 等。

### 6. M0 交付范围

**M0 只交付**：
1. 目录骨架（按 ADR-009）
2. CMake 构建脚本（顶层 + presets）
3. 配置文件（.gitattributes / .gitignore / .clang-format / .editorconfig）
4. 三方件引入逻辑（GTest submodule 启用、ICU port 占位默认关闭）
5. HelloWorld 可执行 + GTest 测试用例验证构建链路

**M0 不交付**（推迟到 M1+）：
- Allocator 接口骨架（ADR-008）
- GC 抽象接口骨架（ADR-002）
- Value 体系形态骨架（ADR-015）
- 实际 ICU 编译接入（M1 Unicode 子系统）
- CI 配置
- README 完整内容（M0 仅占位）

### 7. 验证标准

M0 完成的标志：
- Windows（clang + Ninja）下 `cmake --preset windows && cmake --build build/` 成功。
- WSL/Linux（clang + Ninja）下 `cmake --preset linux && cmake --build build/` 成功。
- HelloWorld 可执行文件运行输出正确。
- `ctest` 跑通 GTest 用例。
- 双平台均 `-Werror` 无警告。

## 后果

- 优点：构建链路早验证、依赖策略清晰、双平台一致、可离线构建（ICU 预下载兜底）。
- 缺点：M0 不出业务代码，仅基础设施；ICU port 占位增加目录复杂度。
- 后续：M1 起填充 Allocator/GC/Value 接口骨架，接入 ICU 编译。
