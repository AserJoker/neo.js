// sema/AnnotationStore.h - AST 节点标注存储
//
// 标注不挂在 Node 上，而是以 NodeId 为键存放在 side table 中：
//   - Node 只增加 4 字节 id，不承担标注存储的固定开销；
//   - 标注生命周期独立于 visitor，可跨多遍 walk 共享；
//   - 节点替换时只要继承 id，标注自然继续生效（见 Node::adoptIdFrom）。
//
// 约定：一个节点对同一标注类型最多一份。NodeId 在单次 parse 内稠密，
// 因此用 vector 按下标索引；未标注的槽位保持空指针。
//
// 关联 ADR: 058（Sema Walker 与标注架构）

#ifndef NEO_SEMA_ANNOTATION_STORE_H
#define NEO_SEMA_ANNOTATION_STORE_H

#include "frontend/Node.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neo::sema {

class IMeta {
public:
    virtual ~IMeta() = default;
};

class AnnotationStore {
public:
    // 若该节点已有同类型标注则返回已有对象；否则构造并返回新对象。
    template <class T, class... Args>
    T& emplace(frontend::NodeId id, Args&&... args) {
        assert(id != frontend::kInvalidNodeId);
        ensure(id);

        auto& slot = slots_[static_cast<std::size_t>(id)];
        if (!slot) {
            slot = std::make_unique<MetaMap>();
        }

        const std::type_index key(typeid(T));
        const auto it = slot->find(key);
        if (it != slot->end()) {
            return static_cast<T&>(*it->second);
        }

        auto meta = std::make_unique<T>(std::forward<Args>(args)...);
        T& reference = *meta;
        slot->emplace(key, std::move(meta));
        return reference;
    }

    template <class T>
    T* get(frontend::NodeId id) {
        return const_cast<T*>(static_cast<const AnnotationStore*>(this)->get<T>(id));
    }

    template <class T>
    const T* get(frontend::NodeId id) const {
        if (id == frontend::kInvalidNodeId ||
            static_cast<std::size_t>(id) >= slots_.size()) {
            return nullptr;
        }
        const auto& slot = slots_[static_cast<std::size_t>(id)];
        if (!slot) return nullptr;

        const auto it = slot->find(std::type_index(typeid(T)));
        if (it == slot->end()) return nullptr;
        return static_cast<const T*>(it->second.get());
    }

    bool empty() const {
        for (const auto& slot : slots_) {
            if (slot && !slot->empty()) return false;
        }
        return true;
    }

    void clear() { slots_.clear(); }

private:
    using MetaMap =
        std::unordered_map<std::type_index, std::unique_ptr<IMeta>>;

    void ensure(frontend::NodeId id) {
        const auto index = static_cast<std::size_t>(id);
        if (index >= slots_.size()) {
            slots_.resize(index + 1);
        }
    }

    std::vector<std::unique_ptr<MetaMap>> slots_;
};

}  // namespace neo::sema

#endif  // NEO_SEMA_ANNOTATION_STORE_H
