# ADR-015: Value 内存模型 — meta+data 与引用计数分层

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-008（内存架构）、ADR-012（Scope 树与 GC）、ADR-016（Type 体系）

## 背景

确定 Value 的内存模型与 GC 的关系。

## 决策

### 1. Value 由两部分组成：meta + data

```
Value（GC 管理的单元）
├── meta：描述类型和操作（类型标签、操作集/接口）
└── data：描述数据
    ├── ref 字段：有多少个 value 指向该 data（引用计数）
    └── （复杂类型）持有子 value 的容器
```

- **meta**：描述类型和操作。
- **data**：描述数据。

### 2. data 的引用计数（ref）

- data 内含 **ref 字段**：有多少个 value 指向它（Object/Function/Array 的引用语义）。
- **Value 持有 data 指针，ref 随指向变化**：
  - 有 Value 指向 data → ref++。
  - 不再指向 → ref--。
  - **ref 归 0 → delete data**（数据销毁）。

### 3. 分层：GC 管 Value，引用计数管 data

- **GC 只管 Value 的销毁**（scope_ref 根 + 图遍历，见 ADR-012）。
- **data 的内存销毁不归 GC 管**：由 data.ref 引用计数决定（ref 归 0 即 delete）。

### 4. 复杂类型的子 value 访问

- Object/Function/Array 的**字段/元素/方法也是 Value**。
- GC 借助 **Value->data 通道**枚举访问子 value（字段、方法、元素）进行标记。
- data 持有子 value 的容器，经 meta/操作集访问。

## 协同机制（data 永不循环）

- **data 永不循环依赖**：data 持有的子 value 是 **Value**（不是 data），
  而 Value 的销毁由 GC 管理（不是 data.ref）。
- data 的引用计数只反映「有多少 Value 指向我」。
- 依赖链单向向下：data → Value（GC 管理）→ data → Value → ...
- **Value 层有环（对象互相引用）时**：环在 Value 层由 GC 处理（不可达一起回收），
  被回收的 Value 指向的 data 随之 ref 递减，最终归 0 销毁。
- **data 的销毁不影响自己子 value 的销毁**（子 value 归 GC 管）。
- 分层：GC 管 Value 生命周期，引用计数管 data 生命周期；两层无环冲突。

> 更正：早期表述「data 循环引用由 GC 兜底」不准确——data 本身不会循环，
> data 持有的是 GC 管理的 Value，Value 销毁驱动 data ref 归零，data 销毁是确定性的。

## 后果

- 优点：Value/data 分层清晰、data 确定性销毁、GC 处理循环、复杂类型可达。
- 缺点：需要维护 data.ref 计数；data 循环引用由 GC 层兜底。
