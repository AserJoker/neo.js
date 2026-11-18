// CollectionBuiltins.h - Map/Set/WeakMap/WeakSet（ES2024 §24）
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_COLLECTION_BUILTINS_H
#define NEO_CORE_PRIVATE_COLLECTION_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 Map/Set/WeakMap/WeakSet 构造函数、原型与迭代器。
// 依赖 Object/Function/Iterator 内建已就绪。
bool registerCollectionBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_COLLECTION_BUILTINS_H
