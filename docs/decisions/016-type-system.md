# ADR-016: Value 的 Type（meta）体系

- 状态: 已接受 (2026-11-02，2026-11-03 修订)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-010（接口-实现-工厂）、ADR-015（Value 内存模型）、ADR-017（类型清单）、ADR-006（异常即值）

## 背景

确定 Value 的 meta（Type）如何提供语言行为。

## 决策

### 1. Type 是无状态单例：TypeKind + 行为接口

- **Type 是抽象类**，封装 Value 的多态行为，作为 Value 的 meta。
- **Type 无状态**：不持任何实例字段，所有行为纯函数（按 ctx + 操作数计算结果）。
- **每种类型一个全局单例**：NumberType / StringType / ObjectType / FunctionType / ...
  所有 Value 持 `Type*` 指针共享同一单例，**无须申请释放、无须引用计数**。
  Type 生命周期与进程一致，GC 不跟踪 Type。
- **TypeKind 枚举**：Type 持一个 `TypeKind` 枚举值（Number/String/Object/...），
  用于快速判同类型、分派调试、序列化。TypeKind 是类型协商的辅助标识，不替代行为接口。

```cpp
enum class TypeKind : uint8_t {
    Undefined, Null, Boolean, Number, String,
    Object, Function, Array, Symbol, BigInt,
    Exception, Fatal, Interrupt,
    // ...
};

class Type {
    TypeKind kind_;
public:
    TypeKind kind() const { return kind_; }
    virtual const char* name() const = 0;

    // 行为接口（纯虚，每类型覆写）——见 §3
    virtual Value* add(Context* ctx, Value* a, Value* b) = 0;
    virtual Value* call(Context* ctx, Value* self, size_t argc, Value** argv) = 0;
    virtual Value* get_property(Context* ctx, Value* self, Value* key) = 0;
    // ...
};
```

### 2. Value 是轻量外壳，行为代理给 Type

- Value = `Type*`（meta）+ `Data*`（数据负载）+ `scope_ref`（GC 跟踪计数）。
- Value 不实现语言行为，所有操作转发给 `type()`：

```
a + b  →  ctx->add(a, b)  →  a->type()->add(ctx, a, b)
```

### 3. 行为方法统一签名：返回 Value*

- 所有行为方法统一返回 `Value*`——正常结果、Exception、Fatal 都走同一通道。
  符合 ADR-006（异常即值）与 ADR-017 §4（Fatal 透传）。
- **调用点统一做 Fatal 检查**：行为方法也是返回 `Value*` 的接口，无例外（ADR-017 §4）。
- 签名风格：

```cpp
// 二元运算
Value* Type::add(Context* ctx, Value* a, Value* b);
Value* Type::sub(Context* ctx, Value* a, Value* b);
// ...

// 单目/自身操作
Value* Type::call(Context* ctx, Value* self, size_t argc, Value** argv);
Value* Type::get_property(Context* ctx, Value* self, Value* key);
Value* Type::set_property(Context* ctx, Value* self, Value* key, Value* val);
Value* Type::to_string(Context* ctx, Value* self);
// ...
```

- `ctx` 传入是为了让 Type 能创建结果 Value（经 Context 的 `create_*` 走 GC 分配）、
  能创建 Exception/Fatal Value 表达错误。

### 4. 分派规则：永远左操作数的 type 执行

- 二元运算 `a + b` 永远分派给**左操作数的 type**：`a->type()->add(ctx, a, b)`。
- **Context 不持协商规则表，纯代理**：`ctx->add(a, b)` 内部仅 `return a->type()->add(ctx, a, b);`。
- JS 的转换规则（ToPrimitive、字符串优先等）**分散在各 Type 实现里**，不由 Context 集中编排。
  每个 Type 在自己的 `add/sub/...` 实现中硬编码它所实现的操作的 JS 转换规则。

```cpp
Value* ContextImpl::add(Value* a, Value* b) {
    return a->type()->add(this, a, b);  // 纯代理
}
```

### 5. 跨类型转换：主动调换左右（规则驱动）

- 当左 type 按所实现操作的 JS 规则判断**应由另一方主导**时，主动调换左右：
  `ctx->add(b, a)` → `b->type()->add(ctx, b, a)`，让右变左由其 type 落实。
- **调换是规则驱动的确定行为，不是"处理不了"的被动 fallback**。例：

```
1 + ""  →  NumberType::add(ctx, 1, "")
         按 JS '+' 规则（任一是 string → string concat），主动调换
         →  ctx->add("", 1)
         →  StringType::add(ctx, "", 1)  落实 concat → "1"
```

  NumberType 调换不是因为"处理不了 string"，而是 JS 规则明确要求 string 主导 `+`。

- **调换不限次数**：逻辑正确的实现按 JS 规则必然收敛到兜底分支；若真发生无限调换，
  自然栈溢出崩溃（视为 Type 实现 bug，不强制防护）。

### 6. 兜底分支：NaN 或 Exception

- 真正无法处理的组合（按 JS 规范定义为 NaN 的场景、操作语义非法）由 Type 内部返回：
  - **NaN**：数值运算的不兼容结果（如 `Number + undefined` 在某些操作下为 NaN）。
  - **Exception**：操作语义非法（如对 number 调 `call`）——Type 内部调
    `ctx->create_exception(...)` 创建 "not a function" 之类的 Exception Value 返回。
- **操作有效性由 Type 自负责**：不合法操作不 panic、不抛 C++ 异常，走 Exception Value 通道。

### 7. 外部一律走 Context 代理

- 外部代码（VM、native 函数、宿主）**不直接调 `type->op(...)`**，一律走 Context：
  `ctx->add(a,b)` / `ctx->call(f,...)` / `ctx->get_property(o,key)` / ...
- Type 的行为方法视为 Type 之间的内部协议，Context 是唯一公开入口。
- 这让 Context 能在代理层统一加 Fatal 检查、GC 触发、作用域跟踪等横切逻辑。

## 分工

```
Value（外壳，代理）
├── meta → Type*（全局单例，共享，无 ref 计数）
└── data → Data*（引用计数负载）

Type（无状态单例）= TypeKind + 行为接口
  ├── NumberType / StringType / ObjectType / FunctionType / ...
  └── 行为方法返回 Value*（正常/Exception/Fatal 统一通道）

Context（纯代理 + 横切）
  ctx->add(a,b) → a->type()->add(ctx, a, b)
  ctx->call(f,...) → f->type()->call(ctx, f, ...)
  // 不持协商规则表，JS 转换规则在各 Type 实现里

跨类型转换 = 主动调换左右（Type 内部按 JS 规则决定）
兜底 = NaN / Exception（Type 内部创建并返回）
```

## 后果

- 优点：Type 多态清晰（接口-实现-工厂）、Value 轻量（纯代理）、
  规则分散（各 Type 硬编码所实现操作的 JS 规则，Context 纯代理无规则表）、
  错误统一（全部走 Value* 通道，Fatal/Exception 一致）。
- 缺点：Value 代理有一层间接；JS 转换规则分散在各 Type，无集中表，规则一致性靠实现者自律；
  需维护 Type 实现类集合。
