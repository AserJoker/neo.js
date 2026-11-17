# ADR-062: 正则引擎 — 独立的 ECMAScript 正则语言运行时

- 状态: 提议中（架构设计）
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-003（Unicode 策略）、ADR-009（源码结构）、ADR-010（接口-实现-工厂）、
  ADR-020（标准库组织）、ADR-024（字节码指令集）、ADR-046（中断信号）、
  ADR-053（Unicode 分类表）、ADR-060（VM 框架）、ADR-061（Compiler 框架）

## 背景

正则此前被列为独立模块延后实现。它不是普通内建函数：语法自成体系、
匹配算法是回溯语义、性能与安全风险集中（ReDoS、栈溢出、Unicode 边界）。
把它直接写进 `RegExpBuiltins` 会导致：

- 解析/编译/执行耦合，无法独立测试与缓存；
- 与 `Value`/`GC`/`Scope` 绑定，无法复用，也难以做安全边界；
- 语法错误位置、步数预算、分配上限没有统一收口点。

同时，项目已有成熟的「Parser → Compiler → Program → VM」链路
（ADR-060/061）。正则本质上是一门小型语言，适合复用同一套架构思想。

## 决策

### 1. 定位：指向性 ECMAScript 引擎，宿主无关

- **只服务 ECMAScript 正则语义**，不追求 PCRE/POSIX 通用方言。
- **不依赖 JS 运行时**：`neo::regex` 不包含 `IContext`、`Value`、`Scope`、
  `GC`、`Exception` 等头文件。
- 引擎实现 ECMAScript 的匹配语义（leftmost-first、捕获回溯、`u`/`v` 模式），
  但通过 `Dialect` 开关控制语法扩展（Annex B、后瞻、反向引用等）。
- 明确非目标：PCRE 兼容、JIT、默认 ICU 依赖、通用正则方言。

### 2. 流水线：pattern → AST → Program → VM

```
pattern + flags + dialect
  → RegexParser      // 语法分析，产出 AST，记录 pattern offset
  → RegexCompiler    // AST → Program（不可变指令流 + 字符类表 + 捕获元数据）
  → RegexVM          // 在 Subject 上执行，产出 MatchResult
```

- `Program` 不可变、可缓存、可共享（`shared_ptr<const Program>`）。
- 编译一次、执行多次；执行状态（回溯栈、捕获 undo log）全部在单次
  `exec` 内部，`Program` 不持有可变状态。
- 与 ADR-060/061 同构：一个节点一个 parse/compile 函数；VM 用分派表
  执行指令；错误显式返回，不静默生成错误产物。

### 3. 原语系统：RegexEngine + RegexHost

`RegexEngine` 是正则侧的门面（类比 `IContext`），`RegexHost` 是宿主环境
（类比 Engine 的宿主侧）。

```cpp
namespace neo::regex {

struct Flags {
    bool global = false;
    bool ignoreCase = false;
    bool multiline = false;
    bool dotAll = false;
    bool sticky = false;
    bool hasIndices = false;
    bool unicode = false;
    bool unicodeSets = false;
};

struct Dialect {
    bool annexB = true;          // Web 兼容语法
    bool allowLookbehind = true;
    bool allowBackrefs = true;
};

struct CompileError {
    size_t offset = 0;           // pattern 内字节/码元偏移
    std::string message;
};

struct CompileResult {
    std::shared_ptr<const Program> program;
    std::optional<CompileError> error;
    bool ok() const { return program != nullptr && !error.has_value(); }
};

struct Capture {
    bool matched = false;
    size_t start = 0;            // UTF-16 code unit 下标
    size_t end = 0;
};

enum class MatchStatus { Matched, NoMatch, Aborted, Oom };

struct MatchResult {
    MatchStatus status = MatchStatus::NoMatch;
    size_t start = 0;
    size_t end = 0;
    std::vector<Capture> captures;  // [0] 为整体匹配
    bool matched() const { return status == MatchStatus::Matched; }
};

struct ExecOptions {
    size_t startIndex = 0;
    bool anchored = false;  // true = sticky：只在 startIndex 尝试
};

class RegexHost {
public:
    virtual ~RegexHost() = default;
    // 编译期：Unicode 属性集、simple case folding、word char 等。
    // 引擎只在 compile 阶段读取，不进入匹配热路径。
    virtual const UnicodeTables& unicode() const = 0;
    // 执行期：每 N 条指令调用一次；返回 true 表示中止本次匹配。
    virtual bool shouldAbort(uint64_t steps) = 0;
};

class RegexEngine {
public:
    explicit RegexEngine(RegexHost& host,
                         std::pmr::memory_resource* memory = nullptr);
    CompileResult compile(std::string_view pattern, Flags flags,
                          Dialect dialect = {});
    MatchResult exec(const Program& program, const Subject& subject,
                     ExecOptions options = {});
};

}  // namespace neo::regex
```

**Host 粒度（已确认）**：

- 编译期取 Unicode 表 / 折叠表，Compiler 把 `\p{...}`、字符类、大小写
  折叠展开成范围表、位图或表指针；
- 运行期热路径**不做 Host 虚调用**，只有每 N 步一次 `shouldAbort()`；
- 分配走 `std::pmr::memory_resource`（默认 `get_default_resource()`），
  JS 适配层可传入包装 `IAllocator` 的资源。

### 4. Subject：统一 UTF-16 码元视图，调用方持有，引擎借用

```cpp
class Subject {
public:
    // ASCII 快速路径：借用 UTF-8 字节，按码元读取（零拷贝）。
    static Subject fromAscii(std::string_view bytes);
    // 非 ASCII：借用调用方持有的 std::u16string。
    static Subject fromUtf16(std::u16string_view units);

    size_t size() const;                 // UTF-16 code unit 数
    char16_t operator[](size_t i) const;
    // u/v 模式：解码一个码点并返回其码元宽度（1 或 2）。
    uint32_t codePointAt(size_t i, size_t& width) const;
};
```

- 语义基线是 **UTF-16 code unit**：非 `u` 模式的 `/./`、`\uD83D` 必须能
  匹配代理的一半；`u`/`v` 模式按码点推进。
- Subject 由调用方构造并持有，引擎只借用，不负责生命周期。
- JS 适配层把 `StringData`（UTF-8/WTF-8）转成 Subject：ASCII 零拷贝，
  非 ASCII 由调用方持有转换后的 `std::u16string`。

### 5. Program 指令集

```
Char(cp)                  // 匹配一个码元/码点
CharClass(index)          // 匹配预编译字符类
Any(dotAll)               // .
Split(x, y)               // 优先 x，失败回退 y
Jmp(x)
Save(slot)                // 捕获边界
AssertStart / AssertEnd
AssertWordBoundary / AssertNotWordBoundary
Backref(group, ignoreCase)
Lookahead(programIndex, negate)
Lookbehind(programIndex, negate)
Mark(slot) / Progress(slot, exitPc)       // 空匹配保护
SetCounter(slot, value) / IncCounter(slot)
CounterLt(slot, value, targetPc)          // 计数循环
Match
```

- 量词统一编译为计数循环：`CounterLt` 保证 `min`/`max`，可选分支用
  `Split`；无上界时省略 `max` 检查，可选分支的 `Mark`/`Progress` 防止
  空匹配死循环。有界量词不展开，避免 `{0,1000000}` 的编译期膨胀。
- 同一套指令流同时服务回溯 VM，并为将来的 Pike VM 线性兜底保留可能。
- 字符类在编译期展开为范围表/位图；`\p{...}` 在编译期解析为范围集。

### 6. 回溯 VM

- **显式回溯栈**（`std::vector<BacktrackFrame>`），不使用 C++ 递归，
  避免深嵌套/后瞻导致宿主栈溢出。
- **捕获 undo log**：`Save` 记录旧值，回溯时按 mark 恢复，避免整组
  捕获数组拷贝。
- **空匹配保护**：量词迭代若本轮未推进且 `min == 0`，停止循环
  （ECMAScript RepeatMatcher 语义）。
- **leftmost-first**：按规范的回溯顺序决定捕获结果，不是 POSIX
  leftmost-longest。
- **步数预算**：每 N 条指令检查 `shouldAbort()`；中止时返回
  `ExecStatus::Aborted`，由宿主决定抛错或返回 null。
- 执行结果状态：`Matched / NoMatch / Aborted / Oom`，不抛 C++ 异常
  （`std::bad_alloc` 在 `exec` 边界转成 `Oom`）。
- 非 sticky 的 `exec` 从 `startIndex` 向后逐个起点尝试；sticky 只尝试
  `startIndex`。`g`/`y` 的 `lastIndex` 更新由 JS 层负责。

**R2 前后瞻与反向引用实现**：

- 前后瞻编译为独立 `SubProgram`（指令/字符类/scratch 独立），捕获槽与
  主程序共享；嵌套前后瞻的子程序统一登记在 `Program::subPrograms`。
- 正向前瞻保留子匹配产生的捕获；负向前瞻无论成败都恢复进入前的捕获
  快照。子匹配失败时同样恢复快照，避免污染外层回溯。
- 后瞻 R2 采用**正向枚举候选起点**（`0..pos`，要求子匹配恰好结束于
  `pos`）：变长后瞻可直接工作，捕获取最左（最长）成功起点。反向匹配
  优化与更精确的 lazy 捕获顺序留 R6。
- 反向引用比较 UTF-16 码元，遵循 `i` 标志；引用未参与匹配的捕获组按
  空串处理（ECMAScript 语义）。
- `d` 标志不需要 VM 额外工作：`MatchResult::Capture` 已携带每个捕获组
  的 `start/end`，R4 由 JS 层据此构造 `indices` 数组与 `groups`。

### 7. Unicode 数据

- 沿用 ADR-053 的「生成表、零依赖」路线，从 UCD 生成：
  - General_Category 区间表；
  - Script / Script_Extensions；
  - 正则需要的 binary properties（Alphabetic、White_Space、ASCII 等）；
  - CaseFolding.txt 的 simple case folding；
  - 复用已有 ID_Start/ID_Continue。
- ASCII 走常量快速路径；非 ASCII 用排序区间 + 二分或两级 trie。
- ICU 保持可选，不作为默认路径；默认构建行为与 ICU 开关无关。

**R3 实现**：

- `tools/gen_unicode_regex_tables.py` 从 Unicode 15.1.0 的 `UnicodeData.txt`、
  `Scripts.txt`、`ScriptExtensions.txt`、`PropList.txt`、
  `DerivedCoreProperties.txt`、`emoji-data.txt`、`PropertyValueAliases.txt`、
  `CaseFolding.txt` 生成 `RegexUnicodeTables.inc`（排序区间 + 属性名索引）。
- `\p{...}` / `\P{...}` 支持：
  - General_Category（短名/长名/分组，如 `Lu`、`Uppercase_Letter`、`L`）；
  - Script / Script_Extensions（`Script=Greek`、`sc=Grek`、`scx=...`）；
  - ECMAScript 白名单内的 binary properties（`ASCII`、`Alphabetic`、
    `White_Space`、`Emoji`、`ID_Start` 等）。
  - 未知属性名返回 `CompileError`；属性在编译期展开为字符类范围表。
- `u` 模式按码点匹配：`Char`/`CharClass`/`Any` 解码 Subject 的码点并按其
  码元宽度推进；`\uD83D` 不再匹配代理对的一半。
- `i+u` 使用 CaseFolding.txt 的 C+S simple folding；字符类匹配检查折叠
  等价类，`\b`/`\B` 的 word 判定同样考虑折叠到 ASCII word 的字符。
- 非 `u` 模式的 `i` 仍保持 ASCII 折叠（ECMAScript Canonicalize 的完整
  Unicode 版本留后续阶段）；Host 注入 Unicode 表的能力保留，R3 默认使用
  模块内生成表。

### 8. 错误模型与缓存

- 编译错误：`CompileError{offset, message}`；JS 层映射为 `SyntaxError`。
- 执行错误：仅 `Aborted / Oom` 状态；不产生 JS 异常对象。
- `RegexEngine` 内部维护 `(pattern, flags, dialect) → shared_ptr<Program>`
  的 LRU 缓存，避免 `new RegExp(用户输入)` 无限增长。
- 正则字面量在 Compiler 阶段调用 `validate/compile` 做早期错误检查；
  运行时通过同一缓存取 Program。

### 9. JS 接入点

| 层 | 工作 |
|---|---|
| Lexer | 已能扫正则字面量；补 flags 合法性早期错误 |
| Parser | 已产出 `RegExpLiteral(pattern, flags)`，无需大改 |
| Sema | 可调用 `regex::validate` 做早期错误 |
| Compiler | 新增 `push_regexp` 指令（pattern/flags 常量下标） |
| VM | `opPushRegExp` → `IContext::createRegExp(pattern, flags)` |
| Context | 新增 `TypeKind::RegExp`、`RegExpData`、`createRegExp`、bootstrap |
| String | 补 `match/matchAll/replace/replaceAll/search/split` 与 `Symbol.*` |

**R4 实现**：

- `TypeKind::RegExp` + `RegExpData`（继承 `ObjectData`）持有
  `[[OriginalSource]]`、`[[OriginalFlags]]`、`shared_ptr<const Program>` 与
  flags 布尔值；`lastIndex` 建为普通自有数据属性（writable / 不可枚举 /
  不可配置）。
- `IContext::createRegExp(pattern, flags)` 编译失败返回 `SyntaxError` 异常值；
  `RegExp` 构造器 call/new 共用创建逻辑，`new RegExp(re)` 复制 source/flags。
- `RegExp.prototype`：`exec`、`test`、`toString`、`source`/`flags`/`global`/
  `ignoreCase`/`multiline`/`dotAll`/`unicode`/`unicodeSets`/`sticky`/
  `hasIndices` 访问器，以及 `@@match`/`@@matchAll`/`@@search`/`@@replace`/
  `@@split`；构造器带 `@@species`。
- `exec` 把 UTF-8 `StringData` 转为 UTF-16 Subject 后调用引擎；结果数组包含
  捕获、`index`、`input`、命名 `groups`，`d` 标志额外提供 `indices`。
- `String.prototype` 六个方法经 `@@match` 等协议转发；`replace` 支持字符串
  替换（`$$`/`$&`/`` $` ``/`$'`/`$n`/`$<name>`）与函数替换。
- Compiler 新增 `push_regexp`（pattern/flags 两个字符串常量下标），VM
  `opPushRegExp` 调用 `IContext::createRegExp`。

`RegExpData` 持有 `shared_ptr<const regex::Program>`，并保存
`[[OriginalSource]]`、`[[OriginalFlags]]`；`lastIndex` 是普通自有数据属性，
由 JS 层读写，不进入引擎。

### 10. 文件布局

```
src/private/regex/
  RegexFlags.h
  RegexDialect.h
  RegexAST.h
  RegexParser.h/.cpp
  RegexProgram.h
  RegexCompiler.h/.cpp
  RegexSubject.h/.cpp
  RegexUnicode.h/.cpp
  RegexUnicodeTables.inc   // 生成产物
  RegexHost.h
  RegexEngine.h/.cpp
  RegexVM.h/.cpp
src/private/core/
  types/RegExp.h
  RegExpBuiltins.h/.cpp
tests/unit/regex/
  RegexParserTest.cpp
  RegexCompilerTest.cpp
  RegexVMTest.cpp
  RegexUnicodeTest.cpp
tests/unit/compiler/
  RegExpTest.cpp          // JS API 端到端
```

### 11. 测试策略

- 引擎单测不依赖 `IContext`：用测试 Host 提供 Unicode 表与 abort 策略。
- 差分测试：同一 pattern/subject 与 Node/V8 对比匹配结果与捕获。
- 模糊测试：libFuzzer 覆盖 Parser/Compiler/VM；ASan/UBSan。
- ReDoS 回归集：已知灾难性回溯 pattern + 步数预算断言。
- 后期接入 test262 的 `built-ins/RegExp`、`language/literals/regexp`、
  `built-ins/String/prototype/*` 子集。

### 12. 分阶段实施

| 阶段 | 内容 | 退出条件 |
|---|---|---|
| **R0** | 模块骨架、Flags/Dialect、RegexHost/RegexEngine、Parser/AST、CompileError | 合法/非法 pattern 单测通过 |
| **R1** | Compiler + Program + 回溯 VM；字面量/字符类/量词/分支/分组/锚点；`g/i/m/s/y`；非 u | 基础 exec/test/lastIndex 端到端 |
| **R2** | 捕获、命名组、反向引用、前瞻/后瞻、`d` indices | 捕获语义与 V8 差分一致 |
| **R3** | u 模式、`\p{...}`、simple case folding、Unicode 生成表 | Unicode 用例通过 |
| **R4** | RegExp 类型/构造器/prototype/Symbol.*、String 六个方法、species | JS API 完整接入 |
| **R5** | v 模式（Unicode sets）、Annex B 兼容 | 规范覆盖 |
| **R6** | Pike VM 线性兜底、前缀扫描、缓存 LRU、ReDoS 预算、fuzz/差分 | 性能与安全收口 |

每阶段一个提交，先补测试再提交；与现有 ADR-061 的推进方式一致。

## 后果

- 优点：
  - 解析/编译/执行解耦，可独立测试、缓存与复用；
  - 安全边界（步数、分配、中断）收口在 `RegexEngine`；
  - 与现有 Parser/Compiler/VM 架构同构，认知成本低；
  - 引擎不依赖 JS 运行时，可作为独立基础库演进。
- 缺点：
  - 需要自研 Parser/Compiler/VM/Unicode 表，初期工作量较大；
  - UTF-8 字符串到 UTF-16 Subject 的转换有成本（ASCII 可零拷贝）；
  - 回溯引擎仍有 ReDoS 风险，需要步数预算与后续线性兜底；
  - `v` 模式与 Annex B 兼容是长期复杂度来源。
- 待定：
  - 步数预算默认值（默认不限 / 高阈值 / 宿主必配）；
  - Pike VM 线性兜底提前到 R3/R4 还是保持 R6；
  - `v` 模式与 Annex B 的优先级；
  - 重复命名捕获组（ES2026 允许不相交分支同名）的实现时机。
