# ADR-010: 组织纪律 — 接口-实现-工厂两层设计

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-007（开发规范）、ADR-009（源码结构）

## 背景

需要确立业务类的组织方式，保证接口隔离，禁止直接依赖实现。

## 决策

### 1. 业务类采用两层设计：接口-实现-工厂

每个业务类由三部分组成：
1. **接口（Interface）**：抽象类（纯虚），定义契约。
2. **实现（Implementation）**：具体类，实现接口（在 src/private/ 中）。
3. **工厂（Factory）**：创建实现，返回接口指针。

```cpp
// src/public/gc/gc.h —— 接口 + 工厂
namespace neo::gc {
class GC {  // 抽象类
public:
    virtual ~GC() = default;
    virtual void Collect() = 0;
    virtual void* Allocate(size_t size) = 0;
};
GC* CreateGC(Allocator* allocator);  // 工厂，返回接口指针
}

// src/private/gc/mark_sweep_gc.h —— 实现
namespace neo::gc {
class MarkSweepGC : public GC { /* ... */ };
}
```

### 2. 核心规则

- **禁止直接依赖实现**：所有代码通过接口（抽象类）交互。
- **存储和接受只接受抽象类**：成员变量、函数参数、返回值都必须是接口类型。
- **模块内**：消费端可以通过 `dynamic_cast` 转换为实现类（使用具体能力）。

### 3. 边界由可见性强制执行

- **`dynamic_cast` 全项目允许**（无需规则限制）。
- **实现类只在模块内可见**（位于 src/private/<mod>/，其他模块不可见）。
- 因此跨模块**无法引用实现类**（编译期拦截），只能走抽象类接口。
- 规则由「可见性」强制执行，而非「约定」——更强的保证。

```
模块 A                        模块 B
src/public/a/a.h     ←可见→   src/public/b/b.h  （只有抽象类）
src/private/a/impl.h 不可见→   （无法引用 A 的实现类）
```

## 价值

- 解耦：消费端只依赖接口，实现可替换。
- 跨模块隔离：模块间只见抽象类，内部细节隐藏。
- 可测试：可注入 mock 实现。
- 可演进：换实现不影响消费者。

## 后果

- 优点：接口隔离强、边界由可见性保证、实现可替换。
- 缺点：需维护接口-实现-工厂三件套（代码量略增）。
