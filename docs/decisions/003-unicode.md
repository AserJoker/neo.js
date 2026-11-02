# ADR-003: Unicode 策略

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent

## 背景

ECMAScript 字符串是 UTF-16 编码的。标准库涉及大量 Unicode 操作：
大小写转换、Unicode 属性（正则 `\p{...}`）、码点遍历、`toUpperCase`/`toLowerCase` 等。
这些在纯手写情况下工作量巨大且极易出错（Unicode 表数据庞大）。

## 决策

1. **核心字符串表示**：自研，UTF-16（与 ECMAScript 规范一致），含 UTF-8/UTF-16/码点转换。
2. **Unicode 增强能力**：允许使用 ICU（业务无关基础库），通过 `D:\projects\icu` 的
   **内嵌方案**集成 —— ICU 数据（icudt*.dat）编译进二进制，运行时调用 `icu_data_init()`
   注册，宿主无需安装 ICU 动态库，保持「可嵌入、零外部运行依赖」。
3. **依赖隔离**：ICU 的使用封装在独立子系统（如 `neo/unicode`），核心引擎主路径不依赖 ICU。
   这样：
   - 无 ICU 时可退化为精简实现（ASCII/基础 Unicode 处理）。
   - 有 ICU 时增强大小写/属性/正则 Unicode 支持。
   - 后期可替换为自研 Unicode 表，不影响其他模块。

## 后果

- 优点：Unicode 处理可靠、开发成本可控、不破坏嵌入卖点、可替换。
- 缺点：引入一个较大的可选构建依赖；需要维护依赖隔离层。
