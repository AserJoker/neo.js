# Scope-Value 体系设计（v1.0）

> 版本：v1.0
> 日期：2026-10-17
> 状态：**待用户确认**
> 关联文档：`docs/architecture.md`（架构）、`docs/coding-standards.md`（编码规范）
> 适用范围：运行时作用域（词法环境）与值生命周期的内部设计，不涉及 C API 表面改动

---

## 一、背景与动机

当前 Scope-Value 是逐条加 API 拼凑出来的（JS_DefineValue / JS_StoreValue / JS_LoadValue），
存在三个结构性问题：

1. **生命周期与绑定双轨并行、互不联动**：`JSScope::values_` 管值生命周期，
   `bindings_` 管 name->value。但绑定引用的值不一定活在绑定所在作用域 →
   实测出现跨作用域 store 后 pop 导致**悬垂指针**（tests 曾失败）。

2. **绑定是裸 `JSValue*`，丢失 JS 语义**：没有绑定类型（var/let/const/参数/catch）、
   没有 const 不可变、没有 TDZ（暂时性死区）。M2 实现这些必然要重构。

3. **"值归属创建它的作用域"与"跨作用域绑定"根本冲突**：JS 语义里赋值只是让
   绑定指向新值，值的生命周期跟 GC 走，不随作用域链。当前模型在闭包/协程
   场景必然悬垂。

架构文档 §6 已定 GC 方向（mark-sweep），§4.3 已定词法作用域 + 闭包 + `with`/`eval`
动态模式。本文档把 M0 到 GC 之间的断档补齐：**作用域只管绑定，值生命周期与作用域解耦**。

---

## 二、设计原则

| # | 原则 | 说明 |
|---|------|------|
| P1 | **作用域 = 词法环境（Environment Record）** | 作用域只负责 name->绑定 的解析，不持有值生命周期 |
| P2 | **值生命周期归 ctx（M0）/ GC 堆（M2+）** | 值与作用域解耦，跨作用域 store、闭包引用天然安全 |
| P3 | **绑定是结构化条目（JSBinding）** | 携带 value/kind/initialized，支撑 const/TDZ/闭包捕获 |
| P4 | **环境链（parent）负责词法查找** | 沿 parent 链向上解析，与 ECMAScript [[OuterEnv]] 一致 |
| P5 | **环境树（children）负责协程/异步挂起** | 挂起时不 pop，子树保留；resume 时继续使用（#7/#8） |

---

## 三、核心结构

### 3.1 三层模型

```
JSContext
 ├── current_scope_ : JSScope*            ← 环境栈顶（current_scope）
 └── values_        : 全局登记表           ← M0 无 GC 阶段的值所有权（M2+ 换成 GC 堆）
      └── 每个 JSValue* 登记于此

JSScope（词法环境）
 ├── parent_  / children_                 ← 环境链 + 环境树
 └── bindings_ : name → JSBinding         ← 本环境声明的绑定（不沿链）

JSBinding（绑定条目）
 ├── value       : JSValue*               ← 当前值（可为 nullptr 表示未初始化）
 ├── kind        : Var|Let|Const|Function|Param|Catch
 └── initialized : bool                   ← TDZ 状态（const/let 声明后初始化前为 false）
```

### 3.2 值生命周期：ctx 全局登记表（M0）

- 所有创建的 `JSValue` **登记在 ctx 级 `values_`**，不再归属某个 scope。
- 作用域 pop 只**清绑定关系**，**不 free 值** → 跨作用域 store、闭包引用安全。
- ctx 析构统一回收所有登记值。
- 临时值等同活到 ctx 结束（M0 可接受；M2+ 由 mark-sweep GC 接管，登记表变为根集起点）。

### 3.3 JSBinding 结构化条目

```cpp
enum class BindingKind : uint8_t {
  Var,      // var 声明（函数级）
  Let,      // let 声明（块级）
  Const,    // const 声明（块级，不可重新赋值）
  Function, // 函数声明
  Param,    // 函数参数
  Catch,    // catch 子句参数
};

struct JSBinding {
  JSValue *value;        // 当前值；未初始化时为 nullptr
  BindingKind kind;
  bool initialized;      // TDZ 状态：let/const 声明后、初始化表达式求值前为 false
};
```

绑定语义（对照 ECMAScript，M0 阶段**只打地基不校验**）：
- **const**：`initialized` 后不可 `store`（重新赋值），违反抛 `TypeError`（#4 错误值）。
  —— M0 先带 kind 参数、**不校验**，M2 编译期真正需要时再启用。
- **let/const TDZ**：声明绑定建立（declarative）到初始化完成之间，`load` 抛 `ReferenceError`。
  —— M0 先带 initialized 字段、**不校验**，M2 启用。
- **var**：初始化即 `initialized=true`，`store` 任意次。

---

## 四、查找与变更语义

### 4.1 解析（Resolve）

沿 `current_scope_` 的 parent 链找持有该 name 的绑定所在环境（`ResolveBinding`）：

```
JSScope *findBindingScope(name)  // 从本环境向上，返回持绑定的环境；未找到返回 nullptr
```

### 4.2 加载（Load，对应 JS_LoadValue）

```
load(name):
  env = resolveBinding(name)
  若 env == nullptr            → ReferenceError（#4 错误值）
  b = env.bindings_[name]
  若 !b.initialized            → ReferenceError（TDZ）
  返回 b.value
```

### 4.3 存储（Store，对应 JS_StoreValue）

```
store(name, value):
  env = resolveBinding(name)
  若 env == nullptr            → ReferenceError（赋值给未声明名）
  若 b.kind == Const           → TypeError（不可重新赋值）
  返回 value
```

### 4.4 定义（Define，对应 JS_DefineValue）

```
define(name, value, kind = Var):
  b = JSBinding{value, kind, initialized = true}
  current_scope_.bindings_[name] = b      // 覆盖式定义到当前环境（不沿链）
```

> 注：`let/const` 的声明（建立绑定、initialized=false）与初始化（赋值）在 JS 中是两个
> 步骤，由编译期拆开。M0 的 JS_DefineValue 是一次性完成（kind 由调用方指定）；
> M2 编译 `let x` 时会先 define(initialized=false) 再在初始化处 store。

---

## 五、C API 设计（不变，语义升级）

| C API | 现状签名 | 新语义 |
|-------|----------|--------|
| `JS_PushScope` / `JS_PopScope` | 不变 | `PopScope` 只清绑定，不 free 值 |
| `JS_DefineValue(ctx, name, value, kind)` | 签名加 `kind` | 定义到当前环境；`value==nullptr` 时定义未初始化绑定（TDZ 起点）。**kind 只记录不校验**（打地基） |
| `JS_StoreValue(ctx, name, value)` | 不变 | 沿链更新已存在绑定；未声明抛 ReferenceError。const/TDZ 校验 M2 启用 |
| `JS_LoadValue(ctx, name)` | 不变 | 沿链查找；未找到抛 ReferenceError。TDZ 校验 M2 启用 |

> `JS_DefineValue` 增加 `kind` 参数（记录绑定类型，为 M2 的 const/TDZ 打地基），
> M0 阶段**不校验** kind 语义；其余 C API 签名不变，宿主仅需为 DefineValue 补一个参数。

---

## 六、内部接口调整

```cpp
// JSScope（去掉 values_，只留绑定）
class JSScope {
 public:
  explicit JSScope(JSAllocator *allocator, JSScope *parent = nullptr);
  ~JSScope();                       // 只销毁绑定关系（不 free 值）

  JSScope *parent() const;
  const std::vector<JSScope *> &children() const;

  JSScope *findBindingScope(const std::string &name);  // 沿链解析（P4）
  JSBinding *findBinding(const std::string &name);     // 沿链解析，返回绑定条目
  void defineBinding(const std::string &name, JSBinding binding); // 定义到本环境

 private:
  JSAllocator *allocator_;
  JSScope *parent_;
  std::vector<JSScope *> children_;
  std::unordered_map<std::string, JSBinding> bindings_;
};
```

```cpp
// JSContext（values_ 上移到这里，作为 M0 全局登记表）
class JSContext {
 public:
  // ... 现有 allocate/deallocate/deallocateSelf/pushScope/popScope/currentScope

  template <typename V>
  JSValue *createValue(V &&value) {          // 值登记到 ctx，不归属 scope
    JSValue *v = allocator_->create<JSValue>(std::forward<V>(value));
    values_[v] = true;
    return v;
  }
  void freeValue(JSValue *v);                // 移出登记表 + destroy

 private:
  JSAllocator *allocator_;
  JSScope *current_scope_;
  std::unordered_map<JSValue *, bool> values_;  // M0 全局登记表（M2+ → GC 根集）
};
```

> `JSScope::createValue` 移除（值不再归 scope）；`registerValue`/`unregisterValue`/
> `freeValue` 移到 JSContext。`setBinding`/`storeBinding`/`getBinding` 由
> `defineBinding`/`findBindingScope`/`findBinding` 替代（返回 JSBinding 而非裸指针）。

---

## 七、与未来里程碑的衔接

| 里程碑 | 衔接点 |
|--------|--------|
| **M1 前端** | 编译期 `Scope` 栈（§4.3）产出绑定类型（var/let/const）；运行时 JSBinding.kind 与之对应 |
| **M2 解释器 + 闭包** | 函数对象持有 `[[Environment]]`（当前环境链）；闭包捕获 = 沿 parent 链的 upvalue 引用；值随 ctx 登记表/GC 存活，闭包不悬垂 |
| **M2 const/TDZ** | JSBinding.kind==Const 拒绝 store；initialized==false 时 load/store 抛错 |
| **#7/#8 协程/async** | 挂起时保留作用域子树（不 pop），resume 继续用；登记表保证值存活 |
| **GC（M2+）** | `values_` 登记表成为根集起点；mark-sweep 沿对象图（含绑定指向的值）遍历；JSBinding 是 GC 遍历环境图的入口 |

---

## 八、对现有测试的影响

现有 12 个测试中，以下需要调整（语义升级所致）：

- **`CApi.LoadValueByScopeChain`**：`createValue` 从 scope 移到 ctx（内部调用点改），
  断言不变（沿链查找行为不变）。
- **`CApi.DefineAndStoreValue`**：跨作用域 store 后 pop 不再悬垂——该测试之前的
  "值必须在绑定作用域创建"的临时规避可移除，直接验证跨作用域 store 安全。
- **`CApi.ScopePushPop`**：`createValue` 调用点改 ctx；其余断言不变。
- 新增测试：const store 抛 TypeError、TDZ load 抛 ReferenceError。

---

## 九、已确认决策（v1.0 用户确认）

1. **`JS_DefineValue` 增加 `kind` 参数，但不校验**：M0 先把签名地基打好（记录绑定类型），
   行为上不强制（const 也能 store、TDZ 不检查），M2 编译期需要时再启用校验。
2. **pop 后绑定值的延迟释放**：没 GC 前只能这样——值登记在 ctx，作用域 pop 后绑定消失
   但值活到 ctx 结束。不做引用计数（与 P2/GC 目标冲突）。

---

*确认后本文档即为 Scope-Value 体系的实现依据。如需调整任何一条，请指出条目编号。*
