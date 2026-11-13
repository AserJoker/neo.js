// sema/Walker.h - AST 遍历与 visitor 分派
//
// Walker 只负责“怎么走”：按 NodeKind 分派、维护 enter/leave 顺序、
// 枚举子节点。语义状态在 SemaContext，标注在 AnnotationStore。
//
// 多遍 walk：每遍创建独立 Walker 并注册该遍需要的 visitor，共享同一个
// SemaContext 与 AnnotationStore。
//
// 遍历顺序：enter(node) → 子节点（源码顺序）→ leave(node)。
// 遍历期间不允许增删子节点；节点替换（rewrite）暂不开放。
//
// 关联 ADR: 058（Sema Walker 与标注架构）

#ifndef NEO_SEMA_WALKER_H
#define NEO_SEMA_WALKER_H

#include "frontend/Node.h"
#include "sema/AnnotationStore.h"
#include "sema/SemaContext.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>

namespace neo::sema {

class Walker;

class IVisitor {
public:
    virtual ~IVisitor() = default;

    virtual void enter(Walker& /*walker*/, SemaContext& /*context*/,
                       frontend::Node& /*node*/) {}
    virtual void leave(Walker& /*walker*/, SemaContext& /*context*/,
                       frontend::Node& /*node*/) {}
};

class Walker {
public:
    explicit Walker(AnnotationStore& annotations) : annotations_(annotations) {}

    Walker(const Walker&) = delete;
    Walker& operator=(const Walker&) = delete;

    // 注册 visitor 关注的节点种类；同一 visitor 可注册多个种类。
    void add(std::unique_ptr<IVisitor> visitor,
             std::initializer_list<frontend::NodeKind> kinds);
    void add(std::unique_ptr<IVisitor> visitor, frontend::NodeKind kind);
    // 关注所有真实节点种类；用于覆盖测试或需要全树统计的 pass。
    void addAll(std::unique_ptr<IVisitor> visitor);

    void walk(frontend::Node& root, SemaContext& context);

    AnnotationStore& annotations() { return annotations_; }
    const AnnotationStore& annotations() const { return annotations_; }

    // 测试/诊断用：某节点种类注册了多少个 visitor。
    std::size_t visitorCount(frontend::NodeKind kind) const;

private:
    static constexpr std::size_t kKindCount =
        static_cast<std::size_t>(frontend::NodeKind::Count);

    void walkNode(frontend::NodePtr& node, SemaContext& context);
    void walkList(frontend::NodeList& list, SemaContext& context);
    void walkChildren(frontend::Node& node, SemaContext& context);

    void dispatchEnter(frontend::Node& node, SemaContext& context);
    void dispatchLeave(frontend::Node& node, SemaContext& context);

    std::array<std::vector<IVisitor*>, kKindCount> visitors_;
    std::vector<std::unique_ptr<IVisitor>> owned_;
    AnnotationStore& annotations_;
};

}  // namespace neo::sema

#endif  // NEO_SEMA_WALKER_H
