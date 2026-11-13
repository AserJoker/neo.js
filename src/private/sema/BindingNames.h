// sema/BindingNames.h - 绑定模式名字收集
//
// 供 StrictnessPass / ScopePass 共用：从 Identifier / ObjectPattern /
// ArrayPattern / AssignmentPattern / RestElement 中收集绑定名。
// 非简单参数（默认值/解构/rest）会把 nonSimple 置真。

#ifndef NEO_SEMA_BINDING_NAMES_H
#define NEO_SEMA_BINDING_NAMES_H

#include "frontend/Node.h"

#include <string>
#include <vector>

namespace neo::sema {

struct BindingName {
    std::string name;
    frontend::SourceRange range;
};

inline void collectBindingNames(frontend::Node* node,
                                std::vector<BindingName>& names,
                                bool& nonSimple) {
    using frontend::NodeKind;

    if (!node) return;

    switch (node->kind()) {
        case NodeKind::Identifier: {
            auto& identifier = static_cast<frontend::Identifier&>(*node);
            names.push_back(BindingName{identifier.name, identifier.range()});
            break;
        }
        case NodeKind::AssignmentPattern: {
            nonSimple = true;
            auto& pattern = static_cast<frontend::AssignmentPattern&>(*node);
            collectBindingNames(pattern.left.get(), names, nonSimple);
            break;
        }
        case NodeKind::ObjectPattern: {
            nonSimple = true;
            auto& pattern = static_cast<frontend::ObjectPattern&>(*node);
            for (frontend::NodePtr& property : pattern.properties) {
                if (!property) continue;
                if (property->kind() == NodeKind::Property) {
                    auto& entry = static_cast<frontend::Property&>(*property);
                    collectBindingNames(entry.value.get(), names, nonSimple);
                } else if (property->kind() == NodeKind::RestElement) {
                    auto& rest = static_cast<frontend::RestElement&>(*property);
                    collectBindingNames(rest.argument.get(), names, nonSimple);
                }
            }
            break;
        }
        case NodeKind::ArrayPattern: {
            nonSimple = true;
            auto& pattern = static_cast<frontend::ArrayPattern&>(*node);
            for (frontend::NodePtr& element : pattern.elements) {
                collectBindingNames(element.get(), names, nonSimple);
            }
            break;
        }
        case NodeKind::RestElement: {
            nonSimple = true;
            auto& rest = static_cast<frontend::RestElement&>(*node);
            collectBindingNames(rest.argument.get(), names, nonSimple);
            break;
        }
        default:
            nonSimple = true;
            break;
    }
}

}  // namespace neo::sema

#endif  // NEO_SEMA_BINDING_NAMES_H
