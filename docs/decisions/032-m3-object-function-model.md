# ADR-032: M3 里程碑 — 对象模型与函数调用语义

- 状态: 已接受 (2026-11-04)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-018（对象模型）、ADR-021（Function 结构）、ADR-022（函数调用）、
          ADR-013（闭包三层作用域）、ADR-014（提升/TDZ/遮蔽）、ADR-015（Value 内存模型）、
          ADR-016（Type 体系）、ADR-030（M2 里程碑）、ADR-031（OOM）

## 背景

M2 完成 Engine 框架 + 扁平类型 + GC + 作用域树 + define/store/scope flags/mutable。
M3 主攻**对象模型与函数调用语义**——让 Engine 能表达和操作复杂值，为后续字节码 VM 奠基。

M3 采用滚动式里程碑：完成一个阶段再定下一个。本 ADR 只定义阶段划分与范围，
各阶段的详细设计在实现前单独评审。

## 决策

### M3 阶段划分

| 阶段 | 范围 |
|------|------|
| 1. Object 类型与属性 | 对象数据结构（属性/保序/符号键/原型指针）、属性描述符、属性增删改查、GC 遍历 |
| 2. Array 类型 | 独立索引元素容器、索引属性、length 语义 |
| 3. Function 类型与 native 调用 | 函数对象结构（native/script 二元标记）、native 函数调用入口 |
| 4. 闭包与三层作用域调用 | 闭包作用域绑定、argument/body 作用域、this 绑定、返回值保活 |
| 5. 原型链与属性查找 | 原型链查找、getter/setter 调用、枚举接口 |

### 阶段边界说明

- **阶段 1**：对象是复杂类型的第一个，属性描述符与 GC 遍历通道是后续 Array/Function
  共同的基础。本阶段只做 Object，不含 Array/Function。
- **阶段 2**：Array 在对象模型基础上增加独立索引容器（ADR-018 §6），复用阶段 1 的
  属性机制存非索引属性。
- **阶段 3**：Function 也是对象（继承阶段 1 的字段），本阶段只实现 **native 调用路径**；
  script 函数（moduleId+pc → VM）留到 VM 阶段。
- **阶段 4**：闭包与调用语义（ADR-013 §5 + ADR-022）。本阶段可先落地 native 调用的
  作用域切换与 this 绑定；script 调用的字节码序言留 VM 阶段。
- **阶段 5**：原型链与访问器属性，是对象模型的语义补全。

### 不在 M3 范围

- script 函数执行（需字节码 VM）。
- Symbol / BigInt / Proxy / Map / Set 等复杂内建。
- 对象的 hidden class / shape 性能优化。
- 前端（Lexer/Parser/Sema/Compiler）与 VM 执行循环。

## 后果

- 优点：Engine 可表达对象/数组/函数，支持 native 调用与闭包语义，为 VM 执行脚本奠基。
- 缺点：M3 无 script 函数执行；复杂内建与性能优化后置。
- M3 完成后下一里程碑（M4）由项目负责人后续定义（预计：前端 Lexer+Parser → 字节码 VM 执行）。
