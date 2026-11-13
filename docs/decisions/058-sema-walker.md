# ADR-058: Sema Walker 与节点标注架构

- 状态: 已接受 (2026-11-13)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-001（Sema 阶段）、ADR-025（两遍 walk）、ADR-057（严格模式在 Sema）

## 背景

Sema 需要多遍遍历 AST：严格性检查、作用域构建、标识符解析与闭包穿透、
控制流检查、模块检查，最后产出 Compiler 可消费的标注。若把全部逻辑塞进
一次 walk，职责会迅速耦合，单测也无法隔离。

因此需要一个可复用的遍历骨架，让每遍 walk 只注册自己关心的节点种类和
处理函数；同时需要一个与 AST 解耦的标注存储。

## 决策

### 1. 三层拆分

| 组件 | 职责 |
|------|------|
| `Walker` | 遍历、按 `NodeKind` 分派、enter/leave 顺序、子节点枚举 |
| `SemaContext` | 作用域栈、严格性栈、错误列表等语义状态 |
| `AnnotationStore` | 节点标注，按 `NodeId` 索引，跨多遍 walk 共享 |

Walker 不持有 JS 语义状态；SemaContext 不负责遍历；AnnotationStore 不依赖
visitor 生命周期。

### 2. 多遍 walk

每遍创建独立 `Walker`，注册该遍需要的 visitor，共享同一个 `SemaContext`
和 `AnnotationStore`：

```cpp
AnnotationStore annotations;
SemaContext context;

Walker strictness(annotations);
strictness.add(std::make_unique<StrictnessVisitor>(),
               {NodeKind::Directive, NodeKind::WithStatement, ...});
strictness.walk(*program, context);

Walker resolve(annotations);
resolve.add(std::make_unique<ResolveVisitor>(), ...);
resolve.walk(*program, context);
```

建议的 pass 顺序：

1. `StrictnessPass`：Directive 栈、legacy octal/escape、`with`、重复参数等。
2. `ScopePass`：建 shadow_scope、变量提升、重复声明。
3. `ResolvePass`：标识符解析、闭包穿透捕获、TDZ、未定义引用。
4. `ControlFlowPass`：`break`/`continue`/label、`return` 位置、
   `new.target`/`super`/`await`/`yield` 上下文。
5. `ModulePass`：import/export 绑定、重复导出、`sourceType` 约束。
6. `AnnotatePass`：输出 Compiler 需要的 capture list、binding kind、
   function flags。

### 3. Visitor 接口

```cpp
class IVisitor {
public:
    virtual ~IVisitor() = default;
    virtual void enter(Walker&, SemaContext&, frontend::Node&) {}
    virtual void leave(Walker&, SemaContext&, frontend::Node&) {}
};
```

- 遍历顺序：`enter(node)` → 子节点（源码顺序）→ `leave(node)`。
- 同一节点种类多个 visitor 按注册顺序执行；enter/leave 顺序一致。
- 注册方式传实例，visitor 可携带构造参数。
- 遍历期间不允许增删子节点。
- 节点替换（rewrite）暂不开放；需要 desugar 时再引入，并明确
  “先 rewrite、再遍历新节点子节点、标注按 id 迁移”的语义。

### 4. 节点 id

`Node` 增加 4 字节 `NodeId`（`uint32_t`，0 为无效值）：

- 每次 `parseProgram` 通过 `NodeIdScope` 从 1 重新分配，保证同一份源码
  的 id 可复现。
- id 在一次 `Program` 生命周期内唯一且稠密；不跨 parse 稳定，不能作为
  持久身份（增量编译/缓存需另加模块维度）。
- `Node::adoptIdFrom` 供“只改变语法形状”的 desugar 继承旧 id，使标注
  继续生效；引入新语义实体时应使用新 id。

### 5. AnnotationStore

标注不挂在 Node 上，而是 side table：

```cpp
template <class T, class... Args>
T& emplace(frontend::NodeId id, Args&&... args);

template <class T>
T* get(frontend::NodeId id) const;
```

- 用 `std::vector` 按下标索引（id 稠密），未标注槽位为空指针。
- 每个节点每种标注类型最多一份；`emplace` 已存在时返回已有对象。
- key 用 `std::type_index`，避免 `string_view` 的生命周期陷阱。
- `IMeta` 只提供虚析构；具体标注类型由各 pass 定义。

### 6. 输出与 scratch 分离

- `AnnotationStore` 中供 Compiler 消费的标注（capture list、binding kind、
  function flags）随 `SemaResult` 输出。
- `shadow_scope` 等 Sema 内部 scratch 由 `SemaContext` 或 pass 局部持有，
  pass 结束即销毁，不进入 Compiler。

### 7. 错误模型

与 `ParseResult` 对称：

```cpp
struct SemaError {
    frontend::SourceLocation location;
    std::string message;
};
```

Sema 采用“收集错误、继续分析”，不 fail-fast；遍历由树结构驱动，天然有界。

## 后果

- 优点：pass 可独立开发与单测；Walker 可复用于非 Sema 遍历；标注查找 O(1)
  且缓存友好；desugar 通过 id 继承保留标注；Node 不需要知道 `IMeta`。
- 缺点：新增 `NodeKind` 时必须同步更新 `Walker::walkChildren`，否则 Debug
  构建触发断言；节点替换需要显式处理 id 迁移。
- 待定：节点 rewrite 的具体 API、pass 间的标注清理策略、增量编译身份模型。

## 实现状态

- M0：`NodeId` / `NodeIdScope`、`AnnotationStore`、`Walker`、`IVisitor`、
  `SemaContext`、全节点子节点枚举与覆盖测试。
- M1：`StrictnessPass`，覆盖严格性作用域、legacy 字面量/转义、`with`、
  `delete identifier`、重复参数、`eval`/`arguments` 绑定与赋值目标、
  严格模式保留字、非简单参数 + `"use strict"`。
- M2：`ScopePass`，建立 shadow_scope 树（Program / closure / arguments /
  body / block / for / catch / switch / class / with / static block），
  完成 var 提升、lexical 绑定、参数与隐式 `arguments`、重复声明与
  var/lexical 冲突检查。
