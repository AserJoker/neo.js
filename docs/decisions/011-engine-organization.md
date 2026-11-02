# ADR-011: Engine 组织架构

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-001（架构）、ADR-005（native 等价性）、ADR-008（内存）、ADR-010（接口-实现-工厂）、ADR-012（Scope 树与 GC）、ADR-013（闭包与作用域）、ADR-014（提升/TDZ/遮蔽）

## 背景

确定语言核心（Engine）的组织架构。

## 决策

### 1. Engine 是模块/设计用语，不是具体类

Engine 是设计用语与模块名，不是类。
Engine 模块包含：Value 体系、对象模型（Object/Function/Array 等）、
作用域（Scope）、标准库（内建对象）、以及核心类 Context。

### 2. Context 是 Engine 的核心类 / 门面

- **Context = Engine 门面**：创建引擎、执行脚本、访问全局对象。
- native 签名中的 `Context* ctx` 即此 Context（ADR-005）——
  native 通过 Context 访问语言核心。
- Context 聚合/持有语言实体与状态：Value 体系、对象模型、作用域、
  调用栈/try栈/label栈、标准库。

```
┌─────────────────────────────────────────┐
│  Context (Engine 门面，核心类)             │
│  - 创建引擎/执行脚本/访问全局               │
│  - 持有：Value/对象模型/作用域/标准库       │
│  - 持有：调用栈/try栈/label栈              │
│  - native 的 ctx 入口                     │
└─────────────────────────────────────────┘
```

### 3. 内存归属

- **Allocator**：外部注入（业务无关，纯基础设施），不由 Context 创建。
- **GC 实例**：Engine 根据配置创建（业务层）。
- **两者都由 Context 内部持有生命周期**（Context 拥有它们的生命周期）。

### 4. 遵循接口-实现-工厂（ADR-010）

- Context 遵循两层设计：
  - 接口：`neo::core::Context`（抽象类，在 src/public/core/）
  - 实现：private/core/ 下具体实现
  - 工厂：`CreateContext(...)` 返回接口指针

### 5. 核心语言模型类（已确定）

核心语言模型层级：

```
Context（门面/根）
  └── Scope（作用域）
        └── Value（值）
```

- **Context**：门面，持有 Scope 与 Value 体系。
- **Scope**：作用域，管理变量的声明/查找/绑定。
- **Value**：值，语言的最小数据单元。

**Environment 仅作为访问器**（对外装饰层），暂时搁置，不是核心实现。
可能用于对 Scope 链的对外访问（如调试器/嵌入层查看变量环境），但核心逻辑在 Scope。

### 6. 其余核心类清单（待定）

- core 模块其余核心类（Object/Function/Array/CallFrame 等）
  由项目负责人后续描述业务模型后确定，当前不定义。

## 后果

- 优点：Engine 模块边界清晰、Context 作为唯一门面、native 统一入口。
- 缺点：核心类清单待定，后续业务模型描述后补充。
