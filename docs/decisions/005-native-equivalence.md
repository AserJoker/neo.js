# ADR-005: native 方法与脚本方法的等价性

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-001（Engine/VM 分离）、ADR-004（执行模型）

## 背景

ADR-001 确立 VM 通过调用 Engine 的 API 完成语言功能（VM → Engine）。
ADR-004 确立 VM 是「一次任务的执行器」。

## 推论（负责人提出）

> Engine 通过 API 可以做到纯 API 调用达成的语言核心逻辑执行。
> 换言之，native 方法和脚本中源码定义的方法，在语言核心关系上完全等价，
> 只是将 VM 解析字节码执行变成了硬编码。

## 决策

### 1. native 与脚本方法在语言核心层面完全等价

- **脚本方法**：VM 读取字节码 → 逐条调用 Engine API → 完成语言逻辑。
- **native 方法**：硬编码 C++ → 直接调用 Engine API → 完成语言逻辑。
- 两者在 **Engine API 层汇合**，从此往上是同一条语言逻辑路径。

### 2. Engine API 必须是「图灵完备的语言操作集合」

Engine 不只提供细粒度操作，必须提供能表达任意 JS 语义的高层操作：
- 属性访问（含 getter/setter 调用）
- 函数调用（含 this 绑定、参数求值顺序）
- new 构造（含构造流程）
- 类型转换（ToPrimitive / ToNumber / ToString 等）

这样 native 方法才能用这些 API 拼出与脚本等价的语义。

### 3. native 方法签名

```cpp
Value* native_fn(Context* ctx, Value* self, size_t argc, Value* argv[]);
```

- `ctx`：Engine 语言上下文（作用域/全局对象/标准库）—— Engine 状态。
- `self`：`this` 绑定值。
- `argc` / `argv[]`：参数。
- 返回 `Value*`。

native 方法的输入输出（self/argv/返回值）与脚本方法完全一致，
差异仅在访问形态（显式参数 vs 通过帧读取），不在语义。

### 4. native 调用也符合「每次调用一个执行上下文」模型

每次函数调用创建执行上下文（VM 或 native 的极简形态）：
- 脚本方法：完整 VM（有字节码）。
- native 方法：仅执行上下文（参数/this/返回值槽），无字节码。

## 推论（副产品）

- 调试器可统一处理脚本/native 方法（断点/单步/栈帧视图基于统一调用模型）。
- 标准库可实现为 native 方法（通过 Engine API），与用户 native 模块同构。
- 外部注册的 native 模块与内建方法无差别。

## 后果

- 优点：调用模型统一、标准库/扩展/内建同构、调试器统一、可嵌入性强。
- 缺点：Engine API 需设计成完整语言操作集合（工作量较大）。
