# ADR-014: 变量提升、TDZ 与遮蔽

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-001（Sema）、ADR-012（Scope 树）、ADR-013（闭包与作用域）

## 背景

确定变量提升（hoisting）、TDZ（暂时性死区）与变量遮蔽的处理机制。

## 决策

### 1. 变量遮蔽是 scope 链的自然结果

- 变量查找沿 scope 链**自底向上**（body → argument → closure → root → global）。
- **找到第一个匹配的名字就返回**，因此内层声明同名变量天然遮蔽外层。
- 这是天然的——不需要额外的遮蔽分析，只是「向上查找，找到即返回」。

### 2. TDZ 用特殊变量类型 UNINITIALIZED 实现

- 新增特殊变量类型 **UNINITIALIZED**。
- 当试图访问 UNINITIALIZED 变量时**发生错误**（TDZ）。
- 不需要特殊的 TDZ 逻辑，只是「访问未初始化变量 → 报错」。

### 3. 变量提升在 Sema 阶段完成

- **变量捕获/变量提升都是 Sema 阶段完成的**（非运行时）。
- Sema 需要将变量定义**移动到对应作用域起始位置**：
  - `var`：函数级/模块级作用域起始位置。
  - 其它（let/const/class 等）：块级作用域起始位置。
- 提升后的初始值：
  - `var` → `undefined`。
  - `let`/`const` → `UNINITIALIZED`（TDZ 哨兵）。

### 4. 真正变量定义处退化为「初始值赋值操作」

- 声明被 Sema 移到作用域起始后，源码中 `let x = 5` 不再是「声明+定义」，
  而是**「赋值操作」**（把 5 赋给已提升的 x 槽）。

### 5. 推论：定义点退化为初始值赋值（字节码层面）

- Sema 第一遍 walk 已完成**建槽 + 设初值**（var→undefined，let/const→UNINITIALIZED，
  function 声明→函数对象，详见 ADR-025）。
- 因此源码中**所有变量定义点**（`let x = 5` / `const a = 1` / `var y = 2`）
  在字节码层面**全部退化为对已存在槽的 `store`**，不再有"声明"语义。
- `const` 的不可变是 **slot 属性**：槽必须先可写才能完成初次 store，
  因此 `set_mutable false`（锁不可变）必须在**初次 store 之后**执行：
  ```
  push_uninitialized; define "a"     # 建槽（可写，初值 UNINITIALIZED）
  push_number 1; store "a"           # 唯一允许的 store（覆盖 UNINITIALIZED）
  load "a"; set_mutable false        # 锁槽不可变
  ```
- `const a;`（无初始化）在 Parser/Sema 阶段即拒绝，不到字节码。

## 综合流程

```
Sema：var 提升到函数级（undefined），let/const 提升到块级（UNINITIALIZED）
运行：访问 UNINITIALIZED → TDZ 报错；访问 undefined → 正常（var 提升）
     let x = 5 编译为「赋值」而非「声明」
查找：scope 链自底向上，首个匹配返回 → 天然遮蔽
```

## 后续待细讲

- 变量提升的具体实现细节（Sema 中如何移动定义、作用域如何建槽）后续细讲。

## 后果

- 优点：遮蔽天然、TDZ 实现简单（特殊类型哨兵）、提升集中在 Sema。
- 缺点：UNINITIALIZED 需要特殊处理（访问报错）。
