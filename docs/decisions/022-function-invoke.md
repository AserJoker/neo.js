# ADR-022: 函数调用 — Function::invoke 统一入口与字节码序言

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-004（执行模型）、ADR-005（native 等价）、ADR-013（闭包）、ADR-021（Function 结构）

## 背景

确定函数调用的完整流程：统一入口、作用域切换、参数传递、状态恢复。

## 决策

### 1. Function 有统一的入口 invoke（含异步、生成器）

```cpp
Value* Function::invoke(Context* ctx, Value* self, size_t argc, Value* argv[]);
```

- **所有调用**（普通/异步/生成器）都走 invoke。
- 签名与 native_fn 一致（ADR-005/021）。

### 2. Function 需要保存自己的模块作用域（rootScope）

- Function 保存自己词法作用域的**模块作用域（rootScope）**。
- invoke 时切换 root_scope = func->data->rootScope（模块隔离）。
- native 的模块作用域后续细谈。

### 3. invoke 完整逻辑

```cpp
Value* Function::invoke(Context* ctx, Value* self, size_t argc, Value* argv[]) {
  // 1. 保存调用前的 root/current scope（调用方视角）
  auto root_scope = ctx->getRootScope();
  auto current_scope = ctx->getCurrentScope();

  // 2. 切换到被调函数的词法模块作用域 + 闭包作用域
  ctx->setRootScope(func->data->rootScope);
  ctx->setCurrentScope(func->data->closureScope);

  // 3. this 绑定：优先函数自己的 self，否则用传入的 self
  auto self_ = func->data->self;
  if (!self_) { self_ = self; }

  // 4. 分发：native 直接调，script 走 VM
  Value* res = nullptr;
  if (func->data->kind == NATIVE) {
    res = func->data->native(ctx, self, argc, argv);
  } else {
    ctx->pushScope();                  // argument scope
    Vm* vm = ctx->createVM();
    for (size_t idx = 0; idx < argc; idx++) {
      vm->push(argv[argc-1-idx]);      // 逆序压栈
    }
    vm->push(self_);
    Module* module = ctx->getModule(func->data->normal.id);
    res = vm->run(ctx, module->bytecodes, func->data->normal.pc);
    delete vm;
  }

  // 5. 恢复调用前状态
  res = ctx->restoreValue(current_scope, res);  // 返回值归属恢复后的 scope
  while (ctx->getCurrentScope() != func->data->closure_scope) {
    ctx->popScope();                   // 弹掉 argument/local scope
  }
  ctx->setCurrentScope(current_scope);
  ctx->setRootScope(root_scope);
  return res;
}
```

### 4. 关键点

- **this 优先级**：`func->data->self`（bind/箭头）优先，无则用调用传入的 self。
- **参数逆序压栈**：`vm->push(argv[argc-1-idx])`，栈上从顶到底是
  argv[0]...argv[argc-1]，然后 **self 在最顶**（最后压入）。
- **native 也切 scope**：native 走同样流程（切 rootScope/currentScope），
  只是直接调 native 指针，不经 VM。
- **restoreValue**：把返回值登记/归属到恢复后的 current_scope（scope_ref++），
  使返回值存活，不随被调函数作用域销毁。

### 5. 字节码序言（函数入口）

```
_add_start:
  push_uninitialized; define "a"    # 参数 a 槽
  push_uninitialized; define "b"    # 参数 b 槽
  push_uninitialized; define "c"    # rest 参数 c 槽
  StoreThis                          # ctx->self = vm->top(); vm->pop()（self 栈顶先消费）
  Store "a"                          # val = top; pop; ctx->store("a", val)
  Store "b"
  RestToArray                        # 栈上剩余全部取出 → array → 压回栈
  Store "c"                          # rest 数组绑定到 c
  push_scope                         # 创建 local_scope（参数绑定之后）
  load "a"; load "b"; add; ret      # 函数体
```

- **参数已在栈上**：实参逆序压栈 + self 栈顶，字节码序言负责定义到形参。
- **形参建槽**：先 push_uninitialized + define（建槽），再 Store（绑定实参）。
- **StoreThis**：消费栈顶 self（self 在栈顶，先消费）。
- **RestToArray**：栈上剩余参数全部取出组装成 array 压回栈，完成 rest 语法。
- **push_scope**：创建 local_scope（在参数绑定之后），
  参数属于 argument_scope，函数体局部变量属于 local_scope。

## 后果

- 优点：统一入口（普通/异步/生成器）、模块隔离（rootScope 切换）、
  native/脚本完全同构（ADR-005）、参数栈传递高效。
- 缺点：invoke 需做多次 scope 切换与恢复（少量开销）。
