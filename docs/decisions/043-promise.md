# ADR-043: Promise — 类型骨架、thenable 兼容与分阶段实施

- 状态: 已接受 (2026-11-09)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-006（异常即值）、ADR-012（Scope）、ADR-018（对象模型）、ADR-019（赋值语义）、
          ADR-023（协程，**不涉及**）、ADR-037（调用栈）、ADR-039（规范错误）、ADR-042（动态根）

## 背景

实现 ES2024 §27.2 的 Promise。`async/await` 依赖 VM 挂起/恢复（ADR-023），
当前 VM 尚不存在，故**明确不在本次范围**——本次只做纯 Promise。

## 决策

### 1. 分阶段里程碑（单步实现 + 测试 + 提交）

| 里程碑 | 范围 |
|--------|------|
| M1 | 类型与内部槽基础设施（本文档主体） |
| M2 | resolving functions、**thenable 兼容**、then/catch/finally、job 投递 |
| M3 | 静态方法 resolve/reject/all/race/allSettled/any + `@@species` |
| M4 | 未处理拒绝上报（`[[PromiseIsHandled]]`）+ 文档收尾 |

### 2. Promise 是「普通对象 + Promise 内部槽」

新增 `TypeKind::Promise`，`PromiseType` 声明 `objectLayout = true`，
`PromiseData : ObjectData` 用专用字段表达内部槽（§27.2.1）：

| 内部槽 | 字段 | 说明 |
|--------|------|------|
| `[[PromiseState]]` | `PromiseState state` | Pending / Fulfilled / Rejected |
| `[[PromiseResult]]` | `Value* result` | 完成值或拒绝原因（Pending 时为 undefined） |
| `[[PromiseReactions]]` | `std::vector<PromiseReaction> reactions` | Pending 期间注册的 reaction |
| `[[PromiseIsHandled]]` | `bool isHandled` | 未处理拒绝上报用（M4） |

`enumerateChildren` 遍历 `result` 与每个 reaction 的 capability（promise/resolve/reject）
和 handler——否则 reaction 里的派生 promise 会被 GC 提前回收。

### 3. thenable 兼容，而非严格 Promise 判定

`resolve(x)` 按 §27.2.1.3.2 Promise Resolve Functions 处理：

1. `x` 是本 Promise 自身 → reject TypeError（自引用）；
2. `x` 不是对象 → 直接 fulfill；
3. 取 `x.then`（读取抛错 → reject）；
4. `then` 不可调用 → 直接 fulfill；
5. `then` 可调用 → **排一个微任务**调用 `then.call(x, resolvePromise, rejectPromise)`，
   两者共享 `alreadyResolved` 单元，只生效一次。

关键：判断依据是「**是否可取到可调用的 then**」，而不是 `x instanceof Promise`。
普通对象、其他实现的对象、以及 `Promise.resolve` 的入参都走同一条路径。
`Promise.all/race/allSettled/any` 的元素处理同样复用此链。

### 4. NativeFn 无 userdata → 用闭包注入

`NativeFn` 是裸函数指针，无法捕获 C++ 状态。新增：

```cpp
Value* IContext::captureValue(Value* fn, std::string_view name, Value* value);
```

直接把任意 Value 注入 `FunctionData::closureScope`（不查作用域链），native 内用
`fn->closureScope->lookup(name)` 取回。用于 resolve/reject 函数携带
「目标 promise + alreadyResolved 单元」、reaction job 携带 capability 等。

选择理由：bound args 也能传值，但 `alreadyResolved` 需要可变单元，闭包注入更直接；
且该能力对后续 Map/Set/Generator 同样有用。

### 5. 不给 Task 加 userdata

reaction 的完整逻辑（derived promise、handler、argument）封装在**自定义 TaskRunner**
里，通过 `postMicrotask(entity, run)` 投递。`TaskRunner` 的 `task` 参数即 entity，
足以携带全部上下文。不为具体任务泛化通用 userdata 机制——那正是自定义 run 的用途。

### 6. Promise 构造函数语义

- `Promise(executor)`（无 new）→ TypeError（§27.2.3.1 step 1），用
  `setFunctionConstructor` 的构造钩子区分 call/construct。
- `new Promise(executor)`：executor 不可调用 → TypeError；同步调用
  `executor(resolve, reject)`；executor 抛错 → reject（M2 接入）。

## 后果

- 优点：复用对象模型，零特化属性代码；内部槽专用字段类型安全；thenable 兼容符合规范，
  可与任意 thenable 互操作；分阶段推进降低单次改动风险。
- 缺点：`PromiseData::reactions` 是 C++ 容器，需手动在 `enumerateChildren` 登记
  （漏登记会导致派生 promise 被提前回收）。
- 范围：不含 `async/await`、`for await`；`Promise.withResolvers`（ES2024 新增）留待后续。
