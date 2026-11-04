# ADR-027: 测试规范与覆盖率门禁

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-007（开发规范：组合>继承、依赖注入）、ADR-026（M0 工具链）

## 背景

确定合入门禁的测试与覆盖率标准，保证工程治理质量。neo.js 作为编译器/VM 运行时，
对可靠性要求高（内存安全、语义正确、跨平台一致），需在合入前卡覆盖率门禁。
本 ADR 配合 ADR-007 的可测试性要求（组合优于继承、依赖注入），使代码天然可测。

## 决策

### 1. 测试分层

```
tests/
├── unit/           # 单元测试：单函数/单类，mock 依赖，快
├── integration/    # 集成测试：多模块协作，真实依赖，端到端
└── regression/     # 回归测试：bug 修复配套用例，防复发
```

| 层 | 范围 | 依赖处理 | 运行频率 |
|----|------|---------|---------|
| **单元测试** | 单函数/单类/单模块 | mock 外部依赖，隔离测试 | 每次提交 |
| **集成测试** | 多模块协作（如 Lexer+Parser、Compiler+VM） | 真实依赖，不 mock | 每次提交 |
| **回归测试** | 历史 bug 的最小复现用例 | 真实依赖 | 每次提交 |

- 每个模块的公开接口（`src/public/<mod>/`）必须有对应单元测试。
- 跨模块流程（如源码→AST→字节码→执行）必须有集成测试。
- 修复 bug 时必须同时提交回归用例（防止复发）。

### 2. 覆盖率指标（合入门禁）

#### 整体门槛

| 指标 | 最低值 |
|------|--------|
| **行覆盖率** | ≥ 90% |
| **分支覆盖率** | ≥ 80% |
| **函数覆盖率** | ≥ 90% |

#### 核心模块上浮（+5~10%）

核心模块可靠性要求更高，指标上浮：

| 模块 | 行覆盖 | 分支覆盖 | 函数覆盖 |
|------|--------|---------|---------|
| **整体** | ≥ 90% | ≥ 80% | ≥ 90% |
| **vm** | ≥ 95% | ≥ 90% | ≥ 95% |
| **gc** | ≥ 95% | ≥ 90% | ≥ 95% |
| **sema** | ≥ 95% | ≥ 85% | ≥ 95% |
| **compiler** | ≥ 95% | ≥ 85% | ≥ 95% |
| **frontend** (lexer/parser) | ≥ 95% | ≥ 90% | ≥ 95% |
| **core** (value/scope/context) | ≥ 95% | ≥ 85% | ≥ 95% |
| 非核心（debug/api/stdlib） | 沿用整体门槛 | | |

- 核心模块分支覆盖要求比行覆盖低 5%，因异常路径/防御性代码分支难全覆盖。
- 新增代码（diff）覆盖率**单独统计**，要求 ≥ 整体门槛（防止新增代码拉低整体）。

#### 不计入覆盖率的代码

- 第三方代码（`third_party/`）。
- 生成的代码（如 ICU 数据嵌入的 `icu_data.c`）。
- 调试/CLI 宿主入口（`main` 函数体，由集成测试覆盖）。

> **不可达分支处理**：遵循 ADR-007 §4，非 C 对外 API 内部信任契约不做防御性判空。
> 不可达的条件分支应直接移除，不用标记排除。真实 OOM/异常路径保留且需测试覆盖。

### 3. 覆盖率采集工具：llvm-cov (source-based)

- **clang 原生 source-based coverage**，与 clang-only 策略（ADR-026）契合。
- 编译标志：`-fprofile-instr-generate -fcoverage-mapping`。
- 链接标志：`-fprofile-instr-generate`。
- 采集流程：
  ```
  1. 编译：-fprofile-instr-generate -fcoverage-mapping
  2. 运行测试：生成 .profraw 文件
  3. 合并：llvm-profdata merge -o coverage.profdata *.profraw
  4. 报告：llvm-cov export/scanse -format=lcov/summary coverage.profdata
  ```
- **精度高**：行、分支、函数覆盖均准确（优于 gcov 的 AST-based）。
- **双平台一致**：Windows/WSL 均用 llvm-cov（随 clang 发行）。

### 4. 门禁时机：M2 起强制

| 阶段 | 门禁 |
|------|------|
| M0 项目搭建 | 无（仅要求测试存在） |
| M1 最小引擎 | 宽松（要求测试存在，不卡覆盖率） |
| **M2 语言核心** | **强制门禁**：覆盖率不达标禁止合入 |
| M3+ | 强制门禁，核心模块指标逐步达标 |

- M0/M1 宽松期：鼓励写测试，但覆盖率不卡合入（骨架代码测试价值低）。
- M2 起强制：语言核心代码（vm/sema/compiler 等）开始有实质逻辑，覆盖率门禁生效。
- 门禁实现：CI 中跑覆盖率脚本，对比指标阈值，不达标 fail。

### 5. 可测试性要求（落实 ADR-007）

代码实现必须保证可测试性，否则覆盖率门禁无法达成：

#### 5.1 组合优于继承

- **避免深度继承链**，用组合 + 接口注入。继承链深 → mock 困难 → 覆盖率难达标。
- 例：`VM` 持有 `Engine*`（组合），不继承 `Engine`。

#### 5.2 依赖注入

- **构造函数注入依赖**，不内部 `new`。可注入 mock → 单元测试可隔离。
- 例：`GC(Allocator* alloc)` 而非 `GC() { this->alloc = new Allocator(); }`。
- 禁止全局单例直接访问（如 `GC::instance()`），改为注入。

#### 5.3 单一职责

- 函数/类职责单一，每个公开接口可独立测试。
- 大函数拆小，每个分支可独立覆盖。

#### 5.4 纯函数优先

- 无副作用的纯函数优先（输入→输出确定），最易测试。
- 有状态逻辑集中在状态对象（如 `Context`），状态对象提供可观察接口。

#### 5.5 接口分离

- `src/public/<mod>/` 暴露接口，`src/private/<mod>/` 实现。
- 测试针对接口写，实现可替换（mock 实现实现接口）。

#### 5.6 FFI 封装原则（可测试性核心约束）

- **FFI 函数不得在业务代码中直接调用**，必须封装成对象/接口后通过依赖注入使用。
- 原因：FFI 函数（`malloc`/`free`/`mmap`/系统调用/clang/llvm 内建/ICU API 等）
  **无法 mock**——它们是外部不可控的真实副作用。若散落在业务代码中，单元测试
  无法隔离其行为，覆盖率门禁无法达成。
- 做法：把同类 FFI 调用**归拢到一个封装对象**中，封装对象对外暴露纯 C++ 接口，
  业务代码依赖该接口。测试时注入 mock 实现替换真实 FFI 封装。
- **首个实例：Allocator（ADR-008）**。`malloc`/`free` 等内存分配 FFI 封装进
  `Allocator` 接口，GC/Value 等业务代码持有 `Allocator*` 而非直接调 `malloc`。
  测试 GC 时注入 mock Allocator 即可隔离真实内存分配。
- 后续 FFI 封装实例（M1+ 落地）：
  - 系统调用（线程/文件/时间）→ `System` 接口
  - ICU API → `UnicodeBackend` 接口（ADR-003，无 ICU 时 mock 为精简实现）
  - 宿主注入的 native 模块 → `HostAPI` 接口（ADR-005）
- 反例（禁止）：
  ```cpp
  // ❌ 业务代码直接调 FFI，无法 mock
  Value* v = (Value*)malloc(sizeof(Value));
  free(ptr);
  ```
- 正例：
  ```cpp
  // ✅ 通过封装接口注入，可 mock
  class Allocator {  // src/public/core/allocator.h
  public:
      virtual void* alloc(size_t n) = 0;
      virtual void  free(void* p) = 0;
  };
  class GC {
      Allocator* alloc_;  // 构造注入
  public:
      explicit GC(Allocator* a) : alloc_(a) {}
      Value* new_value() { return (Value*)alloc_->alloc(sizeof(Value)); }
  };
  ```
- **审计纪律**：CI 静态检查禁止 `malloc`/`free`/`new`/系统调用等 FFI 符号
  出现在 `src/private/` 业务代码中（仅允许出现在封装对象的实现文件内）。
  M2 起随覆盖率门禁一起落地。

### 6. 测试目录组织

```
tests/
├── unit/
│   ├── core/           # Value/Scope/Context 单测
│   ├── gc/             # GC 单测
│   ├── vm/             # VM 单测
│   ├── frontend/       # Lexer/Parser 单测
│   ├── sema/           # Sema 单测
│   ├── compiler/       # Compiler 单测
│   └── CMakeLists.txt
├── integration/
│   ├── pipeline/       # 五阶段端到端
│   └── CMakeLists.txt
├── regression/
│   ├── issue_*.cpp     # 历史 bug 用例
│   └── CMakeLists.txt
└── CMakeLists.txt      # 汇总，注册所有测试
```

- 每个测试可执行对应一个模块或一个集成场景。
- 测试文件命名：`<被测对象>_test.cpp`（如 `value_test.cpp`、`pipeline_test.cpp`）。

### 7. 覆盖率报告生成（CMake 集成）

- CMake 提供 `coverage` preset（启用 `-fprofile-instr-generate -fcoverage-mapping`）。
- `cmake/covgen.py` 辅助脚本：跑测试 → 合并 profraw → 生成 lcov/summary 报告。
- 报告输出到 `build/<preset>/coverage/`。
- 阈值检查脚本对比 per-module 指标，不达标 exit 非零（CI 集成用）。

## 后果

- 优点：质量门禁明确、llvm-cov 双平台一致、可测试性要求倒逼代码质量、
  M2 起强制避免前期被门禁拖慢。
- 缺点：严格指标增加开发成本（核心模块 95% 行覆盖）；可测试性约束限制实现自由度。
- 后续：CI 配置时落地门禁脚本（M2 起）；test262 合规测试（M6）作为补充验证。
