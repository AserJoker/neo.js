# ADR-018: 对象模型

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-015（Value 内存模型）、ADR-016（Type 体系）

## 背景

确定 Object 的对象模型结构。

## 决策

### 1. Object 采用三个容器 + 原型指针

```
Object
├── properties: hashmap (string key -> property)
├── order: vector<string>（只存 key 拷贝，保序遍历）
├── symbols: hashmap (symbol key -> property)
└── prototype: 指针（指向原型对象）
```

- **properties**：hashmap，`string key -> property`（主存储）。
- **order**：vector，只存 key 拷贝，用于遍历时保序。
- **symbols**：hashmap，`symbol key -> property`（符号键分离）。
- **prototype**：指针，指向原型对象。

### 2. property 是描述符（Property Descriptor）

hashmap 存的是 **property（描述符）**，不是裸 Value：

```
property = { value: Value*, writable, enumerable, configurable, get, set }
```

- value：属性值。
- writable / enumerable / configurable：属性特性。
- get / set：访问器属性时的 getter/setter 函数。

### 3. order 存 key 拷贝

- order 存**字符串 key 的拷贝**，只用于遍历时保序。

### 4. 删除语义

- `delete obj.key`：同时从 map 与 order vector 移除（接受 O(n)）。

### 5. GC 遍历通道（衔接 ADR-015）

GC 经 Value->data 通道标记子 value 时，Object 必须暴露：
- prototype 指针（也是 Value，需标记）。
- 所有 property 的 value（含 getter/setter 函数）。
- symbol 属性的 value。
- 键（字符串 Value，若参与 GC 需标记）。

### 6. Array 独立 elements 容器

- **Array 用独立 elements 容器**：`vector<Value*>`（索引元素）。
- 与 properties/order/symbols/prototype 分离（Array 仍持有这些存非索引属性）。
- 索引属性走 elements，命名属性走 properties。

### 7. 枚举：提供各自接口

- 枚举时字符串键（按 order）+ symbol 键（按 symbols 插入序）**合并**。
- 各枚举接口分别提供：
  - for-in：只枚举 enumerable + 原型链上的。
  - Object.keys：自有 enumerable。
  - Reflect.ownKeys：全量（含 symbol）。

## 后果

- 优点：语义正确（保序/符号分离/原型链）、简单可演进（后续可加 hidden class/shape 优化）、Array 性能好（独立 elements）。
- 缺点：order vector 删除 O(n)；字符串 key 在 map 与 order 各存一份。
