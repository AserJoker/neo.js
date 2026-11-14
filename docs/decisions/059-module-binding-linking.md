# ADR-059: 模块绑定、TDZ 与槽位共享 import

- 状态: 已接受 (2026-11-14)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-012（Scope 树）、ADR-013（闭包）、ADR-030（define/store）、
  ADR-044（可达性标记）

## 背景

ES Module 的导入绑定是**导出方绑定的活视图**：导出方重新赋值后，导入方
读取必须看到新值；导入方不能写入。早期实现用 `{namespaceObj, exportKey}`
间接绑定，每次读取都经命名空间属性解析。这能工作，但：

- 每次读取多一次 `getProperty` 与键转换；
- re-export 需要 `IndirectExport` 二次解析；
- 晚到导出、循环依赖依赖运行期“再解析”；
- mutability 挂在 `Value` 上，无法表达“同一个槽位在导出方可写、在导入方只读”。

## 决策

### 1. mutability 属于绑定

`Scope` 的命名绑定改为：

```cpp
struct Binding {
    Value* value = nullptr;
    bool mutableBinding = true;
    bool initialized = true;   // false = TDZ
};
```

- `const` 绑定 `mutableBinding=false`；
- 导入绑定只读由 `BindingKind::Import` 表达，而不是改共享 `Value` 的可变性；
- 闭包捕获复制源绑定属性（const 捕获后仍不可写）。

`Value` 不再持有 `mutable_`。

### 2. TDZ 与初始化

- 提升建槽：`define(name, <uninitialized>, mutable, /*initialized=*/false)`；
- 求值期：`initialize(name, value)` 允许写 `<uninitialized>`，随后 `initialized=true`；
- `store` 对未初始化绑定返回 ReferenceError；
- `load` 是语言级读取：未初始化绑定返回 ReferenceError；
- `Scope::lookup` 仍是内部槽位访问，不触发 TDZ。

初始化时机按声明种类：

| 声明 | 实例化期 | 求值期 |
|---|---|---|
| `var` | `undefined` | 有初始化器则 store |
| `let` | `<uninitialized>` | initialize |
| `const` | `<uninitialized>` | initialize（绑定保持只读） |
| `class` | `<uninitialized>` | initialize |
| `function` | 函数对象 | 无操作 |
| `import` | 别名到导出方槽位 | 无操作 |

### 3. import = 共享槽位

- `Scope::defineImport(localName, slot)`：本地名字别名到导出方同一个 `Value*`；
- 读取直接返回槽位当前值，不再查 namespace；
- `BindingKind::Import` 在 `store` 中拦截写入；
- 闭包捕获通过 `copyImportBindingTo` 复制 Import 绑定，而不是复制当前值；
- 循环依赖时，只要 link 期先建好双方槽位，导入方即可安全持有别名。

### 4. export = 槽位映射

- `exportSlot(exports, exportName, slot)`：导出属性直接指向绑定槽位；
- `getExportSlot`：取槽位而不读取值，供 link 期 import/re-export 使用；
- `exportBinding(exports, exportName, localName)`：本地绑定或导入别名都解析到
  同一个槽位后登记；
- 删除 `IndirectExport` 与读取时二次解析；
- namespace 读取未初始化槽位返回 ReferenceError。

Module Namespace 对象仍然保留，用于 `import * as ns`、动态 `import()`、
`export * as ns`；其属性统一指向槽位。

### 5. link 阶段职责

运行时提供上述原语；完整的 Module Instantiation / Linking 由 Compiler/宿主
按以下顺序编排：

1. 解析依赖图，对每个模块 parse + Sema；
2. 为每个模块建立全部顶层槽位（按上表初始化）；
3. 建立导出表，把 import / re-export 解析为共享槽位；
4. 按 DFS 后序求值模块体。

当前 `importModule` 仍是“惰性加载 + 立即执行”，native loader 需自行先建槽位
并 `exportSlot`，再 `importModule`，最后 `initialize`。这是过渡形态；Compiler
接入后由 link 阶段统一完成。

## 后果

- 优点：import 读取退化为直接槽位访问；re-export 无二次解析；只读与
  const 语义统一在绑定层；TDZ 与提升模型对齐规范。
- 缺点：需要调用方遵守“先建槽位、再 link、最后求值”的顺序；在 Compiler
  完成前，native loader 需要显式模拟 hoisting。
- 待定：完整的模块 link 编排 API、`export *` 名字冲突/`default` 排除的
  静态展开、动态 `import()` 与 namespace 缓存。

## 实现状态

- `Scope::Binding`、`define/bind` 绑定级 mutability、`lookupBinding(Chain)`。
- `define(initialized=false)`、`initialize`、`load`、`store` TDZ 检查。
- `BindingKind::Import`、`defineImport`、`bindImportSlot`、闭包捕获复制。
- `exportSlot`、`getExportSlot`、`exportBinding` 槽位化；`IndirectExport` 删除。
- `ModuleTest` 覆盖槽位别名、只读、TDZ、namespace TDZ、re-export 别名、
  闭包捕获与 GC。
