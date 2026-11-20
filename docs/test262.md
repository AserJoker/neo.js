# Test262 合规跑批（M6-1）

Test262 是 ECMAScript 官方一致性测试套件。本仓库提供 `neo_test262`
跑批工具，用于在单元/集成测试之外验证语言与内建的标准符合性。

## 1. 准备 Test262

Test262 体积较大，不纳入仓库。建议放在 `build/`（已被 `.gitignore` 忽略）：

```bash
git clone --depth 1 https://github.com/tc39/test262.git build/test262
```

也可以通过 `--test262-root` 或环境变量 `TEST262_ROOT` 指向任意检出目录。

## 2. 构建

```bash
cmake --build build/windows --target neo_test262
```

产物：`build/windows/bin/neo_test262.exe`。

## 3. 使用

```bash
# 跑 language/expressions 全部用例
neo_test262 --test262-root build/test262 --suite language/expressions

# 只跑一个子目录，并输出 JSON
neo_test262 --test262-root build/test262 \
  --suite built-ins/Object \
  --json build/object.json

# 过滤路径、限制数量、逐用例打印
neo_test262 --test262-root build/test262 \
  --suite language/expressions/addition \
  --filter bigint --limit 20 --verbose

# 追加跳过未实现 feature
neo_test262 --test262-root build/test262 \
  --skip-features Temporal,decorators
```

默认跳过 `staging/` 与 `intl402/`；需要 Intl 时加 `--include-intl`。

## 4. 执行模型

每个用例（每个 strict 变体）创建独立 `Context`，执行顺序：

1. 安装宿主函数：`print`、`$262.global` / `evalScript` / `gc` /
   `detachArrayBuffer`；
2. 执行 `harness/sta.js`、`harness/assert.js`；
3. 执行元数据 `includes` 列出的 harness 文件；
4. 执行测试源。默认同时跑 sloppy 与 strict 两个变体；
   `onlyStrict` / `noStrict` / `raw` 按 Test262 语义处理。

`negative` 用例按 `phase` 判定：

- `parse`：Parser/Sema/Compiler 阶段必须失败；
- `runtime`：必须抛出指定构造器（按错误对象 `name` 比较）；
- `resolution`：M6-1 跳过（模块解析未接入）。

## 5. 崩溃隔离

引擎尚未覆盖的路径可能触发段错误。工具默认按 `--chunk-size`（默认 25）
启动子进程跑批；若子进程异常退出，父进程会逐条重跑该 chunk，把崩溃用例
标记为 `FAIL`（`errorName=Crash`）并继续，避免一个用例中断全量跑批。

调试 runner 本身时可用 `--no-isolation` 在父进程内直接执行。

## 6. M6-1 支持范围

已支持：

- Script 用例、sloppy/strict 双变体、`onlyStrict`/`noStrict`/`raw`；
- `includes`、`negative`（parse/runtime）、`features` 跳过；
- `$262.evalScript`、`$262.gc`、`$262.detachArrayBuffer`；
- 单用例超时（复用 Context 停止标记）。

暂不支持（跳过）：

- `flags: [module]`、`flags: [async]`、`negative.phase: resolution`；
- `$262.agent`（依赖 `atomicsHelper.js` 的用例）；
- 默认 skip 列表中的未实现 feature（Temporal、decorators、ShadowRealm、
  resizable ArrayBuffer、set-methods 等，见 `main.cpp`）。

## 7. 基线工作流

1. 选一个子集跑批，保存 JSON：
   ```bash
   neo_test262 --test262-root build/test262 \
     --suite built-ins/Array --json build/baseline-array.json
   ```
2. 失败按 `errorName` / `reason` 归类：
   - `Compile`：Compiler 未接的语法/运算符；
   - `Crash`：引擎崩溃（优先修）；
   - `uncaught TypeError` 等：语义/内建缺口；
   - `harness error`：runner 或 harness 兼容问题。
3. 修复后重跑同一命令，对比 JSON 中的 `status` 变化，防止回归。

## 8. 后续（M6-2/M6-3）

- M6-2：固化精选子集基线，建立 feature skip 表与失败分类。
- M6-3：接入 `flags: [async]`（`doneprintHandle.js` + 事件循环）、
  `flags: [module]`（模块链接/求值）、`$262.agent`（Worker + SAB + Atomics）。
