// PrivateName.h - class 私有名（ADR-047 §5）
//
// 私有名不是 JS 可见的 string/symbol：每次 class 求值产生唯一身份，同名 #x
// 在不同 class（含基类/派生类）之间互不冲突。description 只用于错误消息，
// 不参与身份判定。
//
// 生命周期：引用计数。ClassData 的私有定义表与实例 PrivateSlots 各持一份引用，
// 最后一个引用释放时 delete。PrivateName 不持有任何 Value，天然无环，
// 因此走宿主堆引用计数即可（与 SourceText 同一模式），不需要 GC 标记。
//
// 关联 ADR: 015（引用计数管 data 负载）、047（class 引擎模型 §5）

#ifndef NEO_CORE_PRIVATENAME_H
#define NEO_CORE_PRIVATENAME_H

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace neo::core {

struct PrivateName {
    std::string description;  // 仅用于错误消息（不含 '#'）
    size_t refCount = 0;      // 引用计数（单线程引擎）
};

// 创建一个 PrivateName（refCount=1，调用方持有）。
inline PrivateName* createPrivateName(std::string_view description) {
    auto* n = new PrivateName;
    n->description.assign(description);
    n->refCount = 1;
    return n;
}

inline void retainPrivateName(PrivateName* n) {
    if (n != nullptr) ++n->refCount;
}

inline void releasePrivateName(PrivateName* n) {
    if (n != nullptr && --n->refCount == 0) delete n;
}

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATENAME_H
