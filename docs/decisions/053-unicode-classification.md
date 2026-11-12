# ADR-053: Unicode 分类 — ID 表、空白与行终止符

- 状态: 已接受 (2026-11-12)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-052（前端源流）、ADR-027（FFI 封装）

## 背景

词法器需要三类分类：

1. `IdentifierStart` / `IdentifierPart`（ES2024 §12.7）；
2. `WhiteSpace`（§12.2）；
3. `LineTerminator`（§12.2）。

Identifier 的规范基础是 Unicode 的 **ID_Start / ID_Continue**（不是
XID_Start/XID_Continue）。可选方案：

- 引入 libicu，用 `u_hasBinaryProperty(c, UCHAR_ID_START)`；
- 硬编码生成的范围表。

## 决策

### 1. 不引入 ICU，使用生成的区间表

- ICU 为两个谓词引入大依赖、初始化开销和“开/关 ICU 两套行为”的分叉；
  当前 `NEO_ENABLE_ICU` 默认 OFF，前端必须无 ICU 可用。
- 生成表是**版本固定**的：Unicode 升级时重新生成，运行时零依赖、零初始化。

表数据来自 **Unicode 15.1.0**（ES2024 对应版本）的
`DerivedCoreProperties.txt`：

- `ID_Start`：660 个区间；
- `ID_Continue`：769 个区间。

排序区间数组 + 二分查找；ASCII 走常量快速路径。两个表合计约 14KB。

### 2. 生成流程

`tools/gen_unicode_id_tables.py` 解析 `DerivedCoreProperties.txt` 的
`ID_Start` / `ID_Continue` 行，合并相邻区间，输出
`src/private/unicode/UnicodeTables.inc`。

```bash
python tools/gen_unicode_id_tables.py DerivedCoreProperties.txt \
    src/private/unicode/UnicodeTables.inc
```

生成文件头记录 Unicode 版本与来源；升级时替换输入文件并重跑，不改接口。

### 3. ECMAScript 额外字符

```text
IdentifierStart  = ID_Start | $ | _
IdentifierPart   = ID_Continue | $ | _ | ZWNJ | ZWJ
```

- `$`（U+0024）、`_`（U+005F）不在 ID_Start 中，由 `isIdentifierStart` 补；
- ZWNJ（U+200C）/ ZWJ（U+200D）在 Unicode 15.1 的 `ID_Continue` 中已包含
  （Cf），`isIdentifierContinue` 仍显式判断以自描述 ECMAScript 规则；
- `\uXXXX` / `\u{...}` 转义后的码点由词法器重新走这些谓词；
- 不做 NFKC 归一化（ECMAScript 标识符不做 normalization）。

### 4. WhiteSpace / LineTerminator

`WhiteSpace`（§12.2）：

- TAB U+0009、VT U+000B、FF U+000C、SP U+0020、NBSP U+00A0、ZWNBSP U+FEFF；
- 其余 Unicode `Space_Separator`（Zs）：U+1680、U+2000..U+200A、U+202F、
  U+205F、U+3000。

`LineTerminator`：

- LF U+000A、CR U+000D、LS U+2028、PS U+2029。

注意：U+0085 NEL **不是** ECMAScript 空白或行终止符；U+200B ZWSP 也不是。
`CharStream` 的行号同步复用 `isLineTerminator`（CRLF 仍单独处理为一行）。

## 后果

- 优点：无 ICU 依赖；分类结果与构建配置无关；表可复现；ASCII 路径无二分开销。
- 缺点：Unicode 版本需手动升级；两个表约 14KB 常量数据。
- 未覆盖：Unicode 标识符的 NFC/NFKC 归一化（规范不需要）；BOM 处理。
- 测试：`tests/unit/unicode/UnicodeClassificationTest.cpp`（6 例），覆盖
  ASCII/非 ASCII ID、`$`/`_`/ZWNJ/ZWJ、Zs 边界、NEL/ZWSP 排除；全量回归通过。
