# ADR-050: BigInt 基础设施 — 纯 C++ 任意精度整数

- 状态: 已接受 (2026-11-12)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-016（Type 体系）、ADR-018（对象模型）、ADR-039（规范错误）、
          ADR-049（JSON，BigInt 序列化报错）

## 背景

ES2024 §6.1.4 的 BigInt 需要任意精度有符号整数。引擎接入（`TypeKind::BigInt`、
原型、运算符分派、`BigInt()` 内建）涉及大量分散分派点，先独立实现**纯 C++ 类**，
把数学正确性与引擎语义解耦，再单独评审接入方案。

## 决策

### 1. 表示：little-endian uint32 limb

```cpp
bool negative_;               // 零恒为 false，不存在 -0
std::vector<uint32_t> mag_;   // little-endian，无前导零 limb
```

曾考虑「变长 byte 数组 + uint32 对齐分段计算」。最终直接存 uint32 limb：

- limb 天然对齐，运算无需每次把 4 个 byte 拼成 32 位；
- 不存在「不足 4 字节的尾部余数」分支；
- 进位/借位以 64 位中间量表达，语义与 byte 方案完全等价。

不变量：`mag_` 无前导零；`mag_` 为空即零且 `negative_ == false`。

### 2. 运算算法

| 运算 | 算法 |
|------|------|
| 加/减 | magnitude 加减，符号按同号/异号与大小决定 |
| 乘 | schoolbook，`uint64_t` 累加进位 |
| 除/模 | n==1 短除法；否则 Knuth Algorithm D（归一化 + qhat 修正 + 加回） |
| 幂 | 平方求幂，指数为 `uint64_t` |
| 位运算 | 转固定长度二进制补码逐 limb 计算，再按结果符号转回 magnitude |
| 左移 | magnitude 按 word/bit 两段移位 |
| 右移 | 非负：magnitude 右移；负数：`(mag + 2^bits - 1) >> bits`（向下取整） |

### 3. 语义对齐 ECMAScript

- `/`、`%` 向零截断，余数符号跟随被除数；
- 位运算按**无限符号扩展**的补码语义（`-1 & 5 == 5`、`~5 == -6`）；
- `>>` 是算术右移（`-5 >> 1 == -3`）；
- 无 `-0`；
- `pow(0)` 返回 1（含 `0^0`），负指数由引擎层抛 RangeError。

### 4. API 边界

已提供：构造（int/int64/uint64）、`parse`（radix 2..36）、`toString(radix)`、
四则、`divMod`、`pow`、位运算、移位、比较、`bitLength`、`toUint64`/`toInt64`
（低 64 位补码截断，供 `BigInt.asIntN/asUintN`）、`fitsUint64`/`fitsInt64`。

刻意未提供（留给引擎层或后续）：

- `fromDouble` / `toDouble`：`Number(bigint)` 的舍入、`BigInt(number)` 的
  整数校验依赖引擎错误通道；
- 字符串前导空白与 `0x`/`0o`/`0b` 前缀：属于 `BigInt(string)` 的宿主语义，
  由引擎层预处理后调用 `parse`；
- 除零行为：`divMod` 返回 `false` 且不修改输出，引擎层必须先抛 `RangeError`；
  `/`、`%` 运算符以「除数非零」为前置条件（`assert`）。

## 后果

- 优点：数学核心可独立测试（含与 `__int128` 的随机对拍、长除法不变量）；
  引擎接入只需做类型包装与错误映射，不重复实现算术。
- 缺点：`mag_` 是 `std::vector`，引擎接入时需决定 data 层是否直接持有该类
  （引用计数）还是拷贝；除法为 O(n²)，本阶段不考虑优化。
- 测试：`tests/unit/bigint/BigIntTest.cpp`，14 例，含 5000 组随机
  `+ - * & | ^` 对拍、5000 组 32 位除法对拍、500 组长除法不变量。
