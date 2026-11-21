# ADR-065: GC 提前释放与 Value 两域所有权模型

- 状态: 已接受 (2026-11-21)，实现待排期
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-008（Allocator/GC 分层）、ADR-012（Scope 树）、ADR-015（Value meta+data）、
  ADR-019（赋值语义）、ADR-029（生命周期）、ADR-036（闭包作用域孤立化）、
  ADR-044（从作用域树可达）、ADR-059（模块绑定）

## 背景

当前 GC 是 STW 标记-清除：`collect` 从 global 作用域树 + `pinRef` 标记，
sweep 回收全部不可达 Value。`popScope` 只销毁 Scope 本身，作用域内的 Value
全部留给下一次 GC。

但解释器的绝大多数 Value 是**作用域内的普通变量/表达式临时值**，它们的作用域
一退出就必然死亡。把这部分确定性释放掉，可以显著降低 GC 的标记与存活集压力。

同时，当前实现存在一个与设计不符的事实：`obj.v = v`、数组元素、`boundArgs`、
symbol key、getter/setter、prototype 等路径**直接保存同一个 `Value*`**，而设计
要求「Value 仅在闭包/import 活绑定时共享，其余情况复制 Value、共享 data」。
这个偏差使得任何基于「当前作用域是否为唯一根」的提前释放都不安全。

本 ADR 定义两域所有权模型与 `popScope` 确定性释放规则，并列出为使其成立必须
修正的跨域复制点。

## 决策

### 1. Value 两域所有权

| 域 | 定义 | 生命周期判定 | 释放者 |
|---|---|---|---|
| **scope-tracked** | 被至少一个 `Scope::values_` 跟踪 | `scope_ref` + `pinRef` | `popScope` 释放唯一根者；共享者交 GC |
| **data-owned** | 只被某个 `IData` 的字段持有，不属于任何 scope | 从根出发的 tracing 可达性 | GC |
| **closure/import 共享** | 同一 `Value*` 出现在多个 scope 的 `values_` | `scope_ref > 1` | GC（owner 死亡后） |

**两域不交叉**：data 边只能指向 data-owned Value；scope 只能持有 scope-tracked
Value。跨域时复制 Value（新 Value 共享 data）。唯一例外是 closure/import 的
scope→scope 共享，它计入 `scope_ref`。

### 2. `Value::scope_ref`

- `Value` 增加 `uint32_t scopeRef_`（与 `pinRef_`/`markEpoch_` 打包可保持 32B）；
- `Scope::add` → `++scopeRef_`；`Scope::remove` / `Scope::destroySelf` → `--scopeRef_`；
- `Scope::add` 允许重复登记，`scopeRef_` 按**出现次数**计数。`popScope` 时统计
  当前 scope 内的出现次数 `localOccurrences`，判据为
  `scopeRef_ == localOccurrences`（等价于「没有其他 scope 持有」）；
- 语义是**提前释放提示**，不是 GC 的存活判据。GC 仍以「从 global 作用域树可达」为准。

### 3. `popScope` 确定性释放

对当前 scope 的 `values_` 中每个 Value，满足以下全部条件时释放：

```
scopeRef_ == localOccurrences
&& pinRef_ == 0
&& age != 0                 // 不在创建窗口（见 ADR-044 的 epoch 语义）
&& !scope->autoDispose()    // captureRef > 0 的孤儿不释放
```

释放动作：

1. `~Value()`（data ref--；ref 归 0 则析构 + free data）；
2. 从 GC 的 `values_` 表移除：Value 内保存下标 + `swap-remove` O(1)，或标 `dead`
   位由下次 sweep 真正 free；
3. **不递归释放** data-owned 子图：data 销毁后子图不可达，交给 GC。

约束：

- **只在 `popScope`（mutator 上下文）执行**，不在 `destroySelf` 中执行
  （`destroySelf` 可能在 GC sweep 期间被 `FunctionData` 析构调用）；
- 只处理当前 scope 的 `values_`；closureScope 等被 data 拥有的作用域仍由 GC 处理；
- `clearTrackedValuesRecursive` 仅用于 GC 析构 teardown，可不维护 `scope_ref`
  （之后不再 pop）；若测试用它模拟不可达，需同步递减。

### 4. 跨域复制（承重不变量）

| 方向 | 处理 |
|---|---|
| scope-tracked → data 字段 | 创建 **data-owned 副本**（新 Value，共享 data） |
| data-owned → scope（return/arguments/this/绑定） | 创建 **scope-tracked 副本**（新 Value，共享 data） |
| data-owned → data 字段 | 直接共享（同域） |
| scope-tracked → scope（closure/import） | 直接共享，`scopeRef_++` |

`Context::store` 已通过 `replaceMeta` 实现「目标 Value 原地换 data」，属于复制
语义，不需要改。需要修的是直接保存 `Value*` 的路径（见 §6）。

### 5. GC 职责与既有语义不变

- GC 仍从 global 作用域树 + `pinRef` 标记，scope-tracked 与 data-owned 都在
  同一张图里；
- `scope_ref` 不参与根判定，环（如函数 ↔ closureScope）留给 GC，不会重新引入
  ADR-044 的根环问题；
- `IData::ref_` 的 data 引用计数语义不变；
- 泄漏断言（`~MarkSweepGC` 收敛后残留即 abort）保留；
- VM 操作数栈与 C++ 局部变量仍是**借用**，不是根：依赖
  `restoreFrame` 先截断栈再 pop scope、`FunctionInvoke` 先把返回值 add 到
  caller 再 pop 的顺序纪律。

## 当前实现偏差（必须先修）

以下路径当前直接保存同一个 `Value*`，违反「两域不交叉」，会导致
`scope_ref == 1` 时误杀：

| 路径 | 位置 | 现状 | 风险 |
|---|---|---|---|
| 对象属性 | `Object.cpp:670/697/701` `Property{value,...}` | 存同一个 `Value*` | scope→data，pop 误杀 |
| 数组元素 | `Array.cpp:64` `elements[index] = Property{value,...}` | 同上 | 同上 |
| 绑定函数实参 | `Context.cpp:1840` `boundArgs.assign(argv,...)` | 同上 | 同上 |
| symbol key | `Object.cpp:684` `SymbolProperty{key, desc}` | 同上 | 同上 |
| getter/setter | `Object.cpp:722` | 同上 | 同上 |
| prototype / private slot | `ObjectData` 字段 | 同上 | 同上 |
| 返回值 | `FunctionInvoke.cpp:123` `savedCurrent->add(res)` | 同一个 `Value*` 进 caller scope | 安全但保守；若来源 data-owned 则错误 adopted |
| `this` / `new.target` | `FunctionInvoke.cpp:93-96` | 直接 `add` | 同上 |
| arguments | `bindArgumentsObject` | 直接 `bind`/`add` argv | 同上 |

修复规则：按 §4 在跨域点复制；同域直接共享。优先修「scope→data」一类，
它们是误杀的直接来源。

## 实现阶段

1. **`scope_ref` 基础设施**：`Value` 加字段；`Scope::add/remove/destroySelf`
   维护；`popScope` 统计 `localOccurrences`。
2. **跨域复制**：按 §6 清单逐项修复；新增 data-owned Value 的创建路径
   （**不要**复用 `createTracked`，它会加入当前 scope）。
3. **`popScope` 释放**：实现判据、`values_` 安全移除、data ref 递减；
   只释放当前 scope 的普通值。
4. **测试与度量**：单测 + ASan + 统计「pop 可释放比例」与额外分配量。
5. **可选过渡**：若阶段 2 审计量过大，先用每 Value 一个 `escaped` 单调位
   （scope→data 时置位，pop 释放 `!escaped && pinRef==0 && 仅当前 scope`）
   作为过渡，待跨域复制对齐后切到 `scope_ref`。

## 测试清单

- `obj.v = local; pop;` 后 `obj.v` 仍可读；
- 数组元素、symbol key、getter/setter、prototype、private slot 同上；
- `bind(fn, local)` 后 local 作用域 pop，bound 参数仍可读；
- `return local` / `this` / `new.target` 跨作用域后仍可读；
- closure/import 共享值在定义作用域 pop 后仍存活；
- 重复 `add` 的 `localOccurrences` 判定；
- `pinRef > 0` 不释放；
- `captureRef > 0` 的 autoDispose scope 不释放；
- data-owned 子图在父 Value 释放后仍由 GC 正确回收；
- 无泄漏、无 UAF（ASan）。

## 后果

- 优点：普通局部变量/临时值在 `popScope` 确定性释放；GC 只需处理 scope 共享
  依赖图与 data-owned 图，标记压力下降；两域所有权清晰，便于后续分代/并发演进。
- 代价：跨域复制会为属性写入等路径增加 Value 分配；需要全量审计 data 边与
  scope 入口；`Value` 增加 4 字节（可打包保持 32B）；`popScope` 增加一次
  `values_` 扫描（与 `destroySelf` 的清理同量级）。
- 风险：若跨域复制漏点，`scope_ref == 1` 会误杀，表现为 UAF 而非泄漏，
  泄漏断言抓不到。因此阶段 2 必须先于阶段 3。

## 不在本 ADR 范围

以下为独立工作项，另行立项/ADR：

- 并发/增量标记、SATB/插入屏障、safepoint、根快照、并发 sweep 与延迟回收；
- 分代 GC（非移动 young/old + remembered set）；
- 池分配器 / 去掉 `MallocAllocator::records_` 哈希记账；
- 原始值 data 驻留与小整数缓存；
- 标记阶段去 `unordered_set<Scope*>`、显式标记栈、原始值跳过 `enumerateChildren`；
- 自适应 GC 阈值。

## 开放问题

- `scope_ref` 按出现次数计数 vs `Scope::add` 去重（前者无热路径开销，后者判据更简单）；
- data-owned Value 的创建路径与 `values_` 登记方式（不入 scope，但仍在 GC 表中）；
- 跨域复制的分配开销是否可用「来源已是 data-owned 则直接共享」抵消；
- `scope_ref` 与 `pinRef`/`markEpoch` 的字段打包方案；
- 过渡期 `escaped` 位是否与 `scope_ref` 并存，还是直接做完整跨域复制。
