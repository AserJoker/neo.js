# GC 引擎设计（v1.0）

> 版本：v1.0
> 日期：2026-10-19
> 状态：**待用户确认**
> 关联文档：`docs/architecture.md`（架构 §6 内存管理与 GC）、`docs/scope-value.md`（Scope-Value 体系）、`docs/coding-standards.md`（编码规范）
> 适用范围：M2-VM ①~④（GC 引擎：统一 Heap + 标记-清除 + 句柄表 + C API 对接）

---

## 一、背景与动机

M0/M1 的值生命周期是 **ctx 全局登记表 + 手动 free**（`JSContext::values_` + `JS_FreeValue`）：

- 所有 `JSValue` 创建后登记在 `values_`，`JS_FreeValue` 立即析构并释放；
- 作用域（`JSScope`）只管 `name → JSBinding` 绑定，值生命周期与作用域解耦（scope-value.md P2）；
- 绑定里的 `JSValue*` 是**裸指针**，宿主必须保证引用者（Scope 绑定、宿主变量）在 `JS_FreeValue` 之后不再使用。

这套模型有三个结构性限制：

1. **无自动回收**：临时值只能活到 ctx 析构，内存只增不减（scope-value.md §3.2 明言"临时值等同活到 ctx 结束"，M2+ 由 mark-sweep 接管）。
2. **裸指针不安全**：宿主持有一个 `JSValue*`，若另一个引用者 `JS_FreeValue` 了它，宿主立即悬垂。
3. **无法支撑 VM**：M2 字节码执行会产生大量中间值（操作数栈、临时对象、闭包 upvalue），必须靠 GC 回收，不可能逐个手动 free。

架构 §6 已定方向：**标记-清除（mark-sweep）**，先正确性后性能。本文档把设计细化到可落地。

---

## 二、设计原则

| # | 原则 | 说明 |
|---|------|------|
| G1 | **GC 堆统一管理值生命周期** | 所有 `JSValue` 由 `Heap` 分配与回收，取代 ctx 全局登记表 |
| G2 | **宿主经句柄（Handle）访问值** | C API 返回/接收的是句柄（表索引），非裸指针；GC 回收不影响宿主已持有的句柄（QuickJS/V8 经典做法） |
| G3 | **根集决定可达性** | 从根集（Context 全局对象、作用域绑定链、句柄表、后续 VM 栈帧/常量池）出发标记 |
| G4 | **GC 非移动式（mark-sweep）** | 对象地址不变，无需句柄重定向；句柄表的稳定性由表索引保证 |
| G5 | **触发双模式** | 分配计数达到阈值自动触发 + 宿主 `JS_GC()` 手动触发 |
| G6 | **正确性优先** | v1 用 `std::unordered_set` 存堆对象、简单线性扫描标记；性能优化（分代/增量）留演进 |

---

## 三、核心结构

### 3.1 三层模型（改造后）

```
JSContext
 ├── current_scope_ : JSScope*              ← 环境栈顶（不变）
 ├── heap_         : Heap                   ← 新增：GC 堆（统一分配 + mark-sweep）
 ├── handles_      : HandleTable            ← 新增：宿主句柄表（根集之一）
 └── roots_        : 根集（Scope 绑定链等）  ← 标记起点

Heap
 ├── objects_ : unordered_set<GCHeader*>    ← 所有堆对象（GC 管理）
 ├── alloc_count_ : size_t                  ← 自上次 GC 的累计分配数（触发阈值）
 └── gc_threshold_ : size_t                 ← 分配阈值（如 1000）

HandleTable
 ├── slots_ : vector<GCHeader*>             ← 句柄槽（索引 = 句柄）
 └── free_  : vector<uint32_t>              ← 空闲槽位栈（复用）

GCHeader（对象头，JSValue 内嵌）
 ├── kind        : uint8_t                  ← 值类型（复用现有 Kind）
 ├── gc_mark     : uint8_t                  ← 标记位（mark 阶段置 1）
 └── ...（JSValue 现有载荷：bool/number/string）
```

### 3.2 JSValue 改造：内嵌 GC 头 + 引用边

```cpp
class JSValue {
  Kind kind_;                  // 值类型
  bool gc_mark_;               // GC 标记位（mark 阶段置 1，sweep 清除）
  bool bool_;                  // boolean 载荷
  double number_;              // number 载荷
  std::string string_;         // string 载荷
  // M3+ 对象模型：对象属性、数组元素、闭包 upvalue 等引用边
  // std::vector<JSValue*> refs_;  ← 未来对象引用边（mark 阶段遍历）
};
```

**M2 阶段 JSValue 是叶子值**（Undefined/Null/Boolean/Number/String/Exception），无引用边；
对象模型（M3）引入后再加 `refs_` 供 mark 遍历。

### 3.3 句柄表（HandleTable）

- **句柄 = 表索引（uint32_t）**，C API 的 `JSValue*` 在内部是 `Handle`（索引）；
- 宿主创建值 → 分配句柄槽，存入新对象指针，返回 `Handle`；
- 宿主 `JS_FreeValue` → 释放句柄槽（对象交给 GC 判断生死，不立即释放）；
- GC 期间句柄槽**本身就是根**（宿主持有的值不可回收）；
- `Handle` 复用：空闲槽位栈 `free_` 复用，避免表无限膨胀。

> **C ABI 兼容**：`typedef struct JSValue JSValue;` 保持不透明指针类型不变，
> 但 C API 层返回的 `JSValue*` 实际是 `reinterpret_cast<JSValue*>(handle)`（整数索引）。
> 内部 `neojs::JSValue` 与 C `JSValue` 解耦：C 层不再 `reinterpret_cast` 内部对象指针。

### 3.4 根集（Roots）

标记起点（架构 §6）：

1. **句柄表**（`HandleTable` 所有非空槽）；
2. **作用域绑定链**（`current_scope_` 沿 parent/children 遍历所有 `JSBinding::value`）；
3. **Context 全局对象**（M3 引入，当前无）；
4. **VM 操作数栈 / 调用栈帧 / 常量池**（M2-VM 字节码执行时加入）；
5. **模块表**（M5）。

---

## 四、算法：标记-清除（Mark-Sweep）

### 4.1 标记（Mark）

```
mark(roots):
  worklist = [所有根对象]
  while worklist 非空:
    obj = worklist.pop()
    if obj.gc_mark_: continue
    obj.gc_mark_ = true
    for ref in obj.refs_:        // 引用边（M3 对象模型后）
      worklist.push(ref)
```

### 4.2 清除（Sweep）

```
sweep():
  for obj in heap.objects_:
    if obj.gc_mark_:
      obj.gc_mark_ = false      // 清除标记，为下一轮准备
    else:
      heap.destroy(obj)         // 未标记 → 回收（析构 + 释放）
      heap.objects_.erase(obj)
```

### 4.3 触发时机

```
allocValue():
  obj = heap.allocate(size)
  heap.objects_.insert(obj)
  if ++heap.alloc_count_ >= heap.gc_threshold_:
    heap.gc()                    // 自动触发（G5）
  return obj
```

- **自动触发**：分配计数达到阈值（默认 1000）时，在分配返回前执行一次完整 GC。
- **手动触发**：`JS_GC(ctx)` API 供宿主调用（测试/调试/内存敏感场景）。
- **阈值可配**：`JS_SetGCThreshold(ctx, n)`（可选，v1 提供默认即可）。

---

## 五、C API 变更

### 5.1 新增

```c
/* 手动触发一次完整 GC */
void JS_GC(JSContext *ctx);

/* （可选）设置自动 GC 分配阈值；0 = 关闭自动触发 */
void JS_SetGCThreshold(JSContext *ctx, size_t threshold);
```

### 5.2 语义调整

- `JS_FreeValue(ctx, value)`：从**释放对象**改为**释放句柄槽**（对象生死由 GC 判定）。
- `JS_NewContext` / `JS_FreeContext`：创建/销毁 `Heap` + `HandleTable`。
- `JS_CreateX` / `JS_LoadValue` / `JS_StoreValue` / `JS_DefineValue` 返回的 `JSValue*`：句柄。

### 5.3 不变式

- `JS_LoadValue` 返回的句柄，宿主必须 `JS_FreeValue` 或保持到 ctx 结束；
- 句柄在 GC 后仍有效（表索引不变）；
- 返回 `JSValue*` 的接口**永不返回 nullptr**（#14）：句柄 0 预留为无效句柄，接口返回的句柄始终非 0（错误也返回指向 Exception 对象的句柄）。

---

## 六、与现有代码的对接

| 现有代码 | 改造 |
|---------|------|
| `JSContext::values_`（全局登记表） | 删除，替换为 `heap_` + `handles_` |
| `JSContext::createValue/freeValue` | `createValue` → `heap_.allocValue()` + 分配句柄；`freeValue` → 释放句柄槽 |
| `JSContext` 析构 | 回收所有句柄槽 + `heap_` 全部对象 + 作用域链 |
| `JSBinding::value`（裸指针） | 保持内部裸指针（内部对象），但值不再手动 free，由 GC 判定；**作用域绑定链是根集**（标记时遍历） |
| `capi.cc` 的 `reinterpret_cast<JSValue*>(内部对象)` | 改为句柄索引与内部对象互转（`HandleTable::get/set`） |
| `value.cc` 的 JSValue 构造 | 保持，`JSValue` 内嵌 GC 头（kind/gc_mark 由 Heap 管理） |

---

## 七、测试计划

1. **值存活**：创建值 → 手动 GC → 句柄仍有效、值内容不变。
2. **作用域链回收**：pushScope → 绑定值 → popScope（绑定消失）→ GC → 值回收（无泄漏）。
3. **句柄根保护**：宿主持有句柄（未 FreeValue）→ GC → 值存活；FreeValue 后 → GC → 值回收。
4. **阈值自动触发**：创建超过阈值的值 → 自动 GC 触发（用 `objects_.size()` 观测）。
5. **循环回收**（M3 对象模型后）：对象互相引用 → 无根 → GC 回收（mark-sweep 天然处理）。

---

## 八、演进路线

- **M2-VM ①~④**：本设计落地（叶子值 + 句柄表 + 简单 mark-sweep）。
- **M3 对象模型**：JSValue 加 `refs_`（对象属性/数组元素/闭包 upvalue），mark 遍历引用边；对象循环引用可回收。
- **M2+ 性能**：分代（新生代/老年代）、增量标记、分配缓冲（bump allocation）。
- **弱引用**：`WeakMap/WeakSet/WeakRef`（架构 §6）在标记阶段特殊处理。
