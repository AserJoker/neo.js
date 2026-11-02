# Neo.js 项目规划

> 从零自研的可嵌入 ECMAScript 标准运行时
> 更新: 2026-11-02 · 状态: 规划阶段

## 一、愿景与定位

- 无重依赖 · 可嵌入 · 标准覆盖 · 工程治理

从零自研的 ECMAScript 标准运行时（只有 JS 标准库，无 Node 标准库），
本体作为嵌入库供其它项目使用，允许外部注册自定义模块。

## 二、整体架构

```
JS 源码 → [Lexer+Parser 合一](AST) → Sema → Compiler → 字节码 → VM 执行
```

| 层 | 职责 | 状态 |
|----|------|------|
| **Engine** | 语言核心：Value/Scope/Context/Function/对象模型/标准库 | 持久状态 |
| **VM** | 执行中枢：栈模式字节码解释、PC 跳转、指令分发 | 临时状态（一次任务） |

**核心原则**：

- 依赖方向：VM → Engine
- VM 栈模式：操作数在栈上，statement 保证栈平衡
- VM 生命周期：每次函数调用/模块执行创建 → return/halt 销毁
- 挂起/恢复：Engine + VM 状态都保存
- **native/脚本等价**：native 通过 Engine API 硬编码，与脚本语言核心等价
  - 签名：`Value* native_fn(Context* ctx, Value* self, size_t argc, Value* argv[])`
- **异常即值**：异常是特殊 Value，走 return 通道，无跨 VM 栈展开

## 三、模块划分（11 个）

1. **Scanner**（随 Parser 推进，正则/除法上下文切换）
2. **Parser**（递归下降，AST + ASI，与 Scanner 合一）
3. **Sema**（语义检查 + AST 标注：闭包收集等）
4. **Compiler**（AST→字节码）
5. **Engine**（语言核心：状态持有，VM → Engine）
6. **VM**（执行中枢：栈模式解释循环、控制流）
7. **GC**（Mark-Sweep，抽象接口）
8. **Unicode 子系统**（可选 ICU 内嵌）
9. **嵌入 API**（纯 C API + 原生模块注册）
10. **调试**（堆栈/源映射/断点）
11. **宿主**（CLI + REPL）

## 四、里程碑（纵向切片，每步可运行）

- **M0 骨架**：构建 + Value 体系 + GC 抽象
- **M1 最小引擎**：五阶段跑通 `console.log` / 表达式
- **M2 语言核心**：控制流/函数/闭包/对象/数组/字符串
- **M3 标准库**：内建对象全量
- **M4 嵌入 API**：外部模块注册
- **M5 调试**：堆栈/断点
- **M6 合规**：test262 跑批

## 五、文档结构

```
README.md
├── docs/
│   ├── design/      — 架构与模块设计
│   ├── decisions/   — ADR（001-006 已落盘，002 GC 待写）
│   └── spec/        — 标准覆盖清单
```

## 六、开发纪律

设计先行 + 测试先行 · 逻辑拆分细 · 先用例后开发 · 小步提交可验证
