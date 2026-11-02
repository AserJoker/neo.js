# ADR-009: 源码结构、API 风格与内部组织方式

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-007（开发规范）、ADR-008（内存管理）、ADR-010（接口-实现-工厂）

## 背景

需要确定项目的源码结构、对外/对内 API 风格、以及内部组织方式。

## 决策

### 1. 三层头文件架构

```
include/neojs.h      # ① 唯一对外 C API（项目对外嵌入入口）
src/public/<mod>/    # ② 模块间公共头（接口 + 工厂函数）
src/private/<mod>/   # ③ 内部实现头/源（h + c）
```

- **include/neojs.h**：唯一对外 C API。
- **src/public/**：每模块对其他模块提供的公共接口。
  例：GC 接口与工厂函数在 public；具体标记-清除 GC 实现放在 private。
- **src/private/**：内部实现头与源，不对外。

### 2. C API：NJS_ 前缀 + 不透明指针

- C API 全部使用**不透明指针**（opaque pointer）。

```c
typedef struct NJS_Value NJS_Value;
typedef struct NJS_Context NJS_Context;

NJS_Value* NJS_CreateNumber(NJS_Context* ctx, double val);
```

- 前缀用 **NJS_**（区别于 QuickJS 的 JS_、V8 的 V8_，与项目名 neo.js 呼应）。

### 3. C++ 内部：neo::模块 嵌套命名空间 + 驼峰

```cpp
namespace neo::core { class Value { ... }; }
namespace neo::gc   { class GC { ... }; }
namespace neo::vm   { class VM { ... }; }
```

- 类名大驼峰，方法小驼峰（C++ 风格）。
- 与 C API（NJS_ 前缀下划线）区分。

### 4. src 下按模块分子目录

- `src/public/` 与 `src/private/` 下按模块分子目录：
  core / gc / vm / frontend / sema / compiler / stdlib / unicode / api / debug。

## 后果

- 优点：外部只见单一 C API、模块间接口清晰、内部实现隐藏、命名风格统一。
- 缺点：三层结构需要维护 public/private 的边界纪律。
