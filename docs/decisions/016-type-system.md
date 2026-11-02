# ADR-016: Value 的 Type（meta）体系

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-010（接口-实现-工厂）、ADR-015（Value 内存模型）、ADR-017（类型清单）

## 背景

确定 Value 的 meta（Type）如何提供语言行为。

## 决策

### 1. Type 是类（meta），提供所有语言 API

- **Type 是类**，作为 Value 的 meta，提供所有语言 API。
- **Value 上的接口都是代理**：Value 上的方法转发给 type。

```
a + b  →  a.type.add(a, b)
```

### 2. Type 多态：抽象接口 + 每类型实现类

- **Type 是抽象接口**（纯虚），每种 JS 类型一个实现类：
  NumberType / StringType / ObjectType / FunctionType / ArrayType / ...
- 不同实现覆写行为方法，实现不同类型的不同行为。
- 符合「接口-实现-工厂」（ADR-010）。

### 3. 二元运算：统一分派器

- 二元运算（如 `a + b`）走**统一分派器**（如 `Context::Add(a, b)`）。
- 内部按类型处理 JS 语义：ToPrimitive、字符串优先、数值转换等。
- 需要类型特定行为时调用 type 接口。
- 不是简单的「左侧类型决定」——JS 二元运算有复杂转换规则。

### 4. 单目/自身操作：代理给 type

- Value 的单目操作/自身行为代理给 type（`a.type.xxx(a)`）。

## 分工

```
Value（外壳，代理）
├── meta → type（抽象接口）
└── data

type 实现类：NumberType / StringType / ObjectType / FunctionType / ...

二元运算（a+b）→ 统一分派器（Context::Add）：
  内部处理 ToPrimitive/字符串优先/数值转换；类型特定行为调 type 接口
Value 单目/自身操作 → 代理给 type
```

## 后果

- 优点：Type 多态清晰（接口-实现）、Value 轻量（纯代理）、
  二元运算语义集中（统一分派器处理转换规则）。
- 缺点：Value 代理有一层间接；需维护 Type 实现类集合。
