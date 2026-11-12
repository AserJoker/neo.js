# ADR-051: BigInt 引擎接入 — 基本类型与运算

- 状态: 已接受 (2026-11-12)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-016（Type 体系）、ADR-006（异常即值）、ADR-039（规范错误）、
          ADR-050（纯 C++ BigInt 基础设施）

## 背景

ADR-050 已提供纯 C++ `neo::bigint::BigInt`。本阶段把它接成引擎原始值
（`TypeKind::BigInt`），覆盖转换链、运算符与比较。包装类（`BigInt()` 构造、
`BigInt.prototype`、`asIntN/asUintN`）与周边联动（JSON、Object tag）留待后续阶段。

## 决策

### 1. 类型与数据

- `TypeKind::BigInt` 插在 `Number` 之后（`TypeKind` 未被当作数组下标）。
- `BigIntType` 无对象布局（`objectLayout=false`），属性访问走原始值路径。
- `BigIntData` 持 `neo::bigint::BigInt`；**不可变**，`clone` 共享 data（`refInc`），
  与 `StringData` 同策略。
- `IContext::createBigInt(const neo::bigint::BigInt&)`：公共头只前向声明
  `neo::bigint::BigInt`，调用方需包含 `private/bigint/BigInt.h`。这是引擎内部
  原语，暂不把 BigInt 类提升为公共 API。

### 2. 转换链（TypeBehavior.h）

| 转换 | BigInt 行为 |
|------|-------------|
| ToBoolean | 零 → false，其余 true |
| ToString | `BigInt::toString(10)` |
| ToNumber | **TypeError**（`Cannot convert a BigInt value to a number`） |
| ToNumeric | 原样返回；对象先 ToPrimitive(NUMBER) 再递归 |
| ToPropertyKey | 走 ToString（`1n` → `"1"`） |
| 严格相等 / SameValue | 按数学值比较（无 -0、无 NaN） |

### 3. 运算符（OperatorBehavior.h）

- `+`：两侧 ToPrimitive；任一 String → 拼接；否则 ToNumeric。
- `- * / % **`：两侧 ToNumeric。
- **混用 BigInt/Number 一律 TypeError**（`Cannot mix BigInt and other types, use
  explicit conversions`）。
- BigInt 除零、模零 → **RangeError**（`Division by zero`）。
- `**` 负指数 → RangeError；指数超出 `uint64_t` → RangeError（实现上限）。
- 位运算：BigInt 走无限符号扩展补码；`>>>` 对 BigInt → TypeError。
- 移位：移位数也必须是 BigInt；**负移位数反向**（`1n << -1n` → `0n`）；
  超大左移 → RangeError，超大右移 → `0n` / `-1n`。
- 一元 `-` / `~` 直接作用于 BigInt；一元 `+` 走 ToNumber → TypeError。

### 4. 关系比较：BigInt 与 Number 精确比较

不能把 BigInt 转 double（大整数会丢精度）。`compareBigIntDouble` 的做法：

1. NaN → unordered（关系运算结果为 false）；±Infinity 直接按符号；
2. 符号不同直接判定；
3. `frexp(absB, &e)` 得到 `absB = m * 2^e`（0.5 ≤ m < 1），与 `absA.bitLength()` 比较；
4. 位数相等且 `e ≤ 53`：比较整数部分（uint64）+ 小数部分；
5. 位数相等且 `e > 53`：`absB` 必为整数，比较高 53 位；高位相同则
   BigInt 的剩余低位非零即更大。

### 5. 抽象相等（§7.2.14）

- BigInt == Number：用上述精确比较，仅当数学值相等（非整数/NaN/Infinity 不等）。
- BigInt == String：`StringToBigInt` 后比较；非法数字串 → false。
  - 支持 ASCII 空白、可选符号（仅十进制）、`0x/0o/0b` 前缀；空串 → 0n。
- BigInt == Boolean：Boolean 先转 Number，再走 BigInt/Number。
- BigInt == Object：对象 ToPrimitive 后递归。
- BigInt == Symbol / null / undefined：false。

### 6. 错误通道

全部走 `Value*` 异常通道（ADR-006）：TypeError/RangeError 由
`detail::typeError` / `detail::rangeError` 创建，不返回裸哨兵。

### 7. 包装类与内建（第二步）

- `BigInt.prototype` 是**普通对象**（ES2024 §21.2.3，不是 BigInt 包装对象），
  原型为 `Object.prototype`；`createPrimitiveWrapper(BigInt)` 通过
  `setBigIntPrototype` 拿到的原型挂链。
- `BigInt` 可调用、**不可构造**（`new BigInt` → TypeError），但有
  `prototype` 属性（不可写/不可枚举/不可配置）；`length === 1`。
- `BigInt(value)`：先 ToPrimitive(number)；Number 必须为整数（否则 RangeError），
  String 走 StringToBigInt（非法 → SyntaxError），其余走 ToBigInt
  （null/undefined/Symbol/Number 直接传 → TypeError）。
- `BigInt.prototype`：`toString(radix)`（默认 10，radix 越界 RangeError）、
  `valueOf`、`toLocaleString`（无 Intl 时等价 toString）、`@@toStringTag = "BigInt"`。
- `BigInt.asUintN/asIntN(bits, value)`：bits 走 ToIndex（BigInt → TypeError、
  负数/超 2^53-1 → RangeError）；用 `2^bits` 取模，asIntN 再把
  `[2^(bits-1), 2^bits)` 映射到负数。bits 实现上限 `2^24`，超过时仅在结果
  可证明落在范围内才返回原值，否则 RangeError。
- `Number(bigint)`：Number 构造改用 ToNumeric，BigInt 再经 `toDouble()`
  （round-to-nearest-even，溢出 → ±Infinity）。`BigInt::fromDouble` 只接受
  有限整数，供 `BigInt(number)` 校验。
- `1n.toString()` 通过原始值属性访问自动 ToObject（BigInt 包装对象）可用。

### 8. 周边联动（第三步）

- `JSON.stringify` 对 BigInt（含包装对象、对象属性、数组元素）显式抛
  TypeError（`Do not know how to serialize a BigInt`）。
- `Object.prototype.toString`：BigInt 原始值 → `[object BigInt]`；
  BigInt 包装对象按 `[[PrimitiveValue]]` → `[object BigInt]`。
- `Object(bigint)` → BigInt 包装对象（`objectCtor` 走 createPrimitiveWrapper）。

## 后果

- 优点：`1n + 1n` 与 `1 + 1n` 走同一条转换链；BigInt/Number 混用、除零、
  负指数、`>>>` 的错误语义集中在一处；大整数比较与 Number 转换不丢精度。
- 缺点：`IContext` 暴露了私有类型的前向声明；指数受 `uint64_t` 限制；
  `asIntN/asUintN` 的 bits 有实现上限。
- 未覆盖：Unicode 空白（StringToBigInt 仅 ASCII）；`BigInt.prototype.toString`
  的 `radix` 不做 ToIntegerOrInfinity 的 ±Infinity 特判以外的扩展；Intl 格式化。
- 测试：`tests/unit/bigint/BigIntTest.cpp`（15 例，含 double 互转）、
  `tests/unit/core/BigIntTypeTest.cpp`（8 例）、
  `tests/unit/core/BigIntBuiltinsTest.cpp`（6 例）、
  JSON/ObjectBuiltins 的联动用例；全量回归通过。
