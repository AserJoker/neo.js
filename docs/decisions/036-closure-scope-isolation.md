# ADR-036: 闭包作用域孤立化 — 调用期临时挂载 + GC 收敛判定

- 状态: 已接受 (2026-11-06)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-012（Scope 树 + scopeRef）、ADR-013（闭包与三层作用域）、ADR-015（Value meta+data）、
          ADR-021（Function 结构）、ADR-022（函数调用）、ADR-029（Scope-Value-GC 生命周期）

## 背景

ADR-013 规定函数持有 closure_scope，其 parent 是定义词法作用域的 root_scope。
实现上 `createNativeFunction` / `createBoundFunction` 把新建的 closureScope 直接
`root->addChild(closure)`，成为 root 的**长期**子作用域。

后果：root 只在 `~Context` 时递归销毁，因此每个闭包作用域及其 `scopeRef++` 的捕获值
在整个运行期都是 GC 根。函数对象本身不可达后，Value 被回收、FunctionData 被销毁，
但闭包作用域仍挂在 root 下——捕获值永不释放。运行期内存持续增长（最终在 Context
析构时才回收）。

## 决策

### 1. 闭包作用域是孤立作用域

- `closureScope` 创建时 `parent = nullptr`，**不**挂到 root 下。
- 生命周期随 `FunctionData`：`FunctionData::~FunctionData()` 调
  `closureScope->destroySelf()`（析构 + `allocator->free`），其 values 逐个 `scopeRef--`。
- FunctionData 是 data 层对象，由 GC sweep 在最后一个引用它的 Value 被回收时销毁
  （ADR-015 分层：GC 管 Value，引用计数管 data）。

### 2. 调用期临时挂载 / 摘除

- 进入**最外层**调用（`activeCalls == 0`）时 `closureScope->reparent(rootScope)`，
  查找链 `execScope → closureScope → rootScope → global` 成立。
- 退出最外层调用时 `reparent(nullptr)` 摘回孤立。
- 重入（递归/回调）只增减 `activeCalls` 计数，不重复挂载/摘除——否则内层调用退出
  会提前摘除，破坏外层仍需要的查找链。
- Fatal 透传路径虽然不恢复现场、不平衡栈，但**仍须摘除**：否则闭包作用域挂在 root
  下，`~Context` 的 root 递归析构会销毁它，随后 FunctionData 析构再 `destroySelf`
  就是悬垂访问。遗留的 execScope 是 closureScope 的子，随闭包作用域一起销毁。

`Scope::reparent(newParent)` 同步维护 `parent_` 与两侧 `children_`（幂等）。

### 3. GC 收敛判定取代「values_ 非空」

孤立化后，`values_` 在 collect 期间不再是静态集合：sweep 阶段销毁 data 会连带销毁
其持有的作用域（FunctionData → closureScope），从而对**其他 Value** 做 `scopeRef--`。
这些 Value 在本轮标记阶段仍是根（已标记存活），要到**下一轮**才被回收。

因此 `~MarkSweepGC` 的泄漏判据不能是「collect 后 values_ 非空」，而是：

```
while (!values_.empty()) {
    size_t before = collectedCount_;
    collect();
    if (collectedCount_ == before) break;   // 本轮净销毁量 == 0：收敛
}
if (!values_.empty()) abort();              // 收敛后仍残留 = 真泄漏
```

运行期单轮 collect 不受影响：本轮新死亡的 Value 留到下一轮，语义正确。

## 后果

- 优点：闭包作用域及其捕获值随函数对象一起回收，运行期不再泄漏；作用域树只保留
  真正的调用期结构；Fatal 路径无双重销毁。
- 缺点：每次最外层调用多两次 `reparent`（各 O(1) 的 set 操作）；GC 析构需循环收敛
  （仅发生在 Context 析构，代价可忽略）。
- 约束：`FunctionData::closureScope` 是 data 自有资源，`clone` 共享 data 时闭包作用域
  也随之共享（同一函数的多个 Value 视图共用一份闭包），与 ADR-019 §2 一致。
