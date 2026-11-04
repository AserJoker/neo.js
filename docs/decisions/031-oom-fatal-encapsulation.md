# ADR-031: M2 阶段 5 — OOM Fatal 封装

- 状态: 已接受 (2026-11-04)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-030（M2 里程碑 §阶段 5）、ADR-006（异常即值）、ADR-017 §4（Fatal）、
          ADR-029 §6（OOM 哨兵）

## 背景

ADR-030 定义的 M2 阶段 5 是 M2 的最后一块拼图。当前 `Context::create*` 在分配失败时
直接返回 nullptr，调用方需判空，且丢失"异常即值"统一通道（ADR-006）。

## 决策

### 范围

1. **OOM Fatal Value 预分配**：Context 构造时最早分配一个 Fatal 类型 Value 作为 OOM
   哨兵，挂载到 global scope（存活于 Context 生命周期）。分配失败则 abort。
2. **create\* 返回哨兵**：所有 `create*` 在分配失败时返回该 OOM 哨兵（不返回 nullptr），
   保持返回 `Value*` 的统一通道。data 已分配但 Value 创建失败时需回收游离 data。
3. **查询接口**：Context 提供 OOM 哨兵的查询接口，调用方用指针比较判断是否 OOM。
4. **Fatal 透传**：OOM 属 Fatal（不可捕获），调用点检查到 Fatal 立即透传，沿用 ADR-017 §4。

### 不在本阶段范围

- 具体错误消息格式、诊断信息拼接（由最外层宿主处理）。
- 非 OOM 类 Fatal 的封装策略。

### 测试要点

- 配额充足时 create\* 正常返回；oom 哨兵非空且为 Fatal 类型。
- 配额耗尽时 create\* 返回同一哨兵实例，不崩溃、不返回 nullptr。
- 哨兵在 Context 存活期间不被 GC 回收，Context 析构后无泄漏。

## 后果

- 优点：Engine 层错误统一走 Value 通道，调用方无需判空；哨兵指针比较高效。
- 缺点：每个 Context 常驻一个 Fatal Value（可忽略）；调用点需遵守 Fatal 透传纪律。
