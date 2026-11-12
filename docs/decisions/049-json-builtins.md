# ADR-049: JSON — 手写解析器/序列化器，直接构造与访问引擎对象

- 状态: 已接受 (2026-11-12)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-006（异常即值）、ADR-018（对象模型）、ADR-020（标准库组织）、
          ADR-039（规范错误）、ADR-048（内部方法层）

## 背景

实现 ES2024 §25.5 的 `JSON.parse` / `JSON.stringify`。

候选方案是引入第三方 JSON 库（nlohmann/json 等）做文本 ↔ DOM，再在 DOM 与
`Value*` 之间搬运。**否决该方案**：JS 引擎需要的不是「C++ 操作 JSON 的能力」，
而是 JSON 文本与**活对象**之间的访问细节与序列化过程：

- `parse` 必须直接把结果建成引擎对象，且 `__proto__` 是自有数据属性、
  重复键保留首次出现的位置；
- `reviver` 的每一步都经 `[[Get]]` / `[[Delete]]` / `[[DefineOwnProperty]]`，
  Proxy/getter 可观察；
- `stringify` 的每一步都经 `[[Get]]` / `[[OwnPropertyKeys]]`，`toJSON`、
  `replacer`、getter、Proxy 全部可观察，还要做循环检测。

中间 DOM 会丢失这些可观察性，且需要额外一次全量拷贝。因此手写递归下降解析器
与图遍历序列化器，全程只操作 `Value*`。

## 决策

### 1. 分阶段实施

| 阶段 | 范围 |
|------|------|
| P1 | `parse` 核心：严格文法、直接构造 Value、数字/字符串/代理 |
| P2 | `reviver`：InternalizeJSONProperty |
| P3 | `stringify` 核心：toJSON/replacer/space/循环检测 |
| P4 | Unicode 边界、测试与本文档 |

### 2. Unicode：UTF-8 + WTF-8

引擎字符串是 UTF-8 `std::string`，且允许孤立代理（`"\uD800"`）。新增
`src/private/unicode/Utf8.h/.cpp`：

- `decodeCodePoint` 接受标准 UTF-8 与 WTF-8（代理按 3 字节编码）；
- `encodeCodePoint` 对代理输出 3 字节 WTF-8；
- `isHighSurrogate` / `isLowSurrogate` / `combineSurrogates` 用于 `\uXXXX` 配对。

这样 `parse("\"\\uD800\"")` 得到的字符串可被 `stringify` 原样还原为
`"\ud800"`，孤立代理不丢失、不报错。

### 3. 严格数字文法

`parse` 只接受 JSON 数字文法（`-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?`），
拒绝 `01`、`+1`、`.5`、`1.`、`0x10`、`NaN`、`Infinity`。数值转换用
`std::from_chars`（locale 无关、可移植）。

`from_chars` 对溢出/下溢统一返回 `result_out_of_range`，无法区分方向。实现按
指数符号近似判断：

- 指数为负且量级 > 308 → 下溢，得到 `±0`；
- 其余 → 溢出，得到 `±Infinity`。

这是**近似**：例如 `1e-400` 正确下溢为 0，但极端尾数组合的边界可能与规范
舍入略有差异。本阶段接受该误差，正确性优先于完备性。

### 4. parse 直接构造对象

- 对象用 `ctx->defineProperty(obj, key, value, true, true, true)` 逐键建立，
  因此 `__proto__` 是自有数据属性（不改原型），重复键覆盖值但保留首次位置。
- 数组逐索引 `defineProperty`，与 ADR-048 的数组逐元素 Property 化一致。
- 深度上限 `kMaxJsonDepth = 1000`，超过抛 `RangeError`（防栈溢出）。
- 语法错误抛 `SyntaxError`，消息带位置信息。

### 5. reviver：InternalizeJSONProperty

按 §25.5.1.1：

1. `[[Get]]` 取当前值；对象则按 `ownPropertyKeys(enumerableOnly)` 递归，
   数组按 `length` 逐索引递归；
2. 递归返回 `undefined` → `[[Delete]]`，否则 `[[DefineOwnProperty]]`；
3. 最后 `Call(reviver, holder, [key, value])`。

全程走内部方法，Proxy/getter/删除语义可观察。

### 6. stringify：图遍历 + 可观察访问

- `initReplacer`：函数 replacer 直接使用；数组 replacer 取元素（字符串/数字/
  包装对象解包）转属性列表并去重。
- `initSpace`：数字取 `min(10, ToInteger)` 个空格；字符串取前 10 个**码点**。
- `serializeProperty`：`[[Get]]` → `toJSON`（可调用则调用）→ replacer →
  包装对象解包 → 按类型分派；对象属性按 `ownPropertyKeys(enumerableOnly)`，
  跳过 `undefined`/函数/Symbol。
- 循环检测用「当前栈上的对象集合」，命中抛 `TypeError`。
- `QuoteJSONString` 实现 well-formed：孤立代理输出 `\uXXXX`，控制字符转义。

### 7. 方法元数据

`JSON.parse.length === 2`、`JSON.stringify.length === 3`，但实参校验下限为 0
（`parse()` 少参应报语法错误而非「缺少参数」）。因此 `defineMethod` 先用
`createNativeFunction(fn, 0)` 创建，再单独 `defineProperty("length", 2/3)`——
与 ADR-043 §5.4 的 `lengthOverride` 是同一类问题。

`JSON[@@toStringTag] === "JSON"`。

## 后果

- 优点：零第三方依赖；parse/stringify 全程可观察，Proxy/getter/toJSON/replacer
  语义与规范一致；直接构造 `Value*`，无中间 DOM 拷贝。
- 缺点：`from_chars` 溢出/下溢为近似判断；深度上限 1000 是宿主选择而非规范值。
- 范围：`BigInt` 尚未实现，序列化 BigInt 走 `TypeError` 分支；`JSON.rawJSON` /
  `JSON.isRawJSON`（ES2025）不在本次范围。
- 测试：`tests/unit/core/JSONTest.cpp` 覆盖 parse 文法/转义/代理/reviver、
  stringify 的 toJSON/replacer/space/循环/Proxy/getter、命名空间元数据。
