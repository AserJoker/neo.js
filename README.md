# neo.js

> 从零自研的可嵌入 ECMAScript 标准运行时 · C++20

## 概述

neo.js 是一个无重依赖、可嵌入、标准覆盖、工程治理导向的 ECMAScript 运行时。
本体作为嵌入库供其它项目使用，允许外部注册自定义模块。只有 JavaScript 标准库，
无 Node 标准库。

**当前状态**：M0 项目搭建阶段（目录骨架 + 构建系统 + 配置文件）。

## 构建

### 前置依赖

- **Clang 14+**（双平台唯一编译器，不支持 MSVC）
- **Ninja**
- **CMake 3.20+**
- **Git**（拉取 GTest submodule）

### 快速开始

```bash
# 克隆（含 submodule）
git clone --recurse-submodules <repo>
cd neo.js

# Windows / WSL 选对应 preset
cmake --preset windows      # 或 linux
cmake --build build/windows # 或 build/linux

# 运行 HelloWorld
./build/windows/bin/neo_hello

# 跑测试
ctest --test-dir build/windows --output-on-failure
```

### ICU（可选，默认关闭）

ICU 通过 FetchContent（本地 port）引入，预下载包放入
`third_party/icu/download/`（已 gitignore），网络受限环境下手工放入：

```
third_party/icu/download/
├── icu4c-74_2-src.tgz
└── icu4c-74_2-data-bin-l.zip
```

启用：`cmake --preset windows -DNEO_ENABLE_ICU=ON`

## 文档

- [交接文档](docs/HANDOFF.md) — 新会话快速接手
- [项目规划](docs/plan.md) — 愿景与里程碑
- [架构决策](docs/decisions/) — ADR 001-026
- [设计文档](docs/design/) — 模块设计（M1 起充实）

## 许可证

（待定）
