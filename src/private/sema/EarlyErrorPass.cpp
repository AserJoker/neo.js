// sema/EarlyErrorPass.cpp - 统一早期错误 walk 实现

#include "sema/EarlyErrorPass.h"

#include "sema/ControlFlowPass.h"
#include "sema/ModulePass.h"
#include "sema/StrictnessPass.h"
#include "sema/Walker.h"

namespace neo::sema {

namespace {

// B.3.1：对象字面量中重复的 __proto__ 数据属性是 SyntaxError。
class DuplicateProtoVisitor final : public IVisitor {
public:
    void enter(Walker&, SemaContext& context, frontend::Node& node) override {
        if (node.kind() != frontend::NodeKind::ObjectExpression) return;
        auto& object = static_cast<frontend::ObjectExpression&>(node);
        bool seen = false;
        for (const frontend::NodePtr& property : object.properties) {
            if (!property || property->kind() != frontend::NodeKind::Property) {
                continue;
            }
            const auto& prop = static_cast<const frontend::Property&>(*property);
            if (prop.computed || prop.shorthand || prop.method ||
                prop.propertyKind != frontend::PropertyKind::Init || !prop.key ||
                prop.key->kind() != frontend::NodeKind::Identifier) {
                continue;
            }
            if (static_cast<const frontend::Identifier&>(*prop.key).name !=
                "__proto__") {
                continue;
            }
            if (seen) {
                context.error(prop.range().start,
                              "Duplicate __proto__ fields are not allowed in "
                              "object literals");
                return;
            }
            seen = true;
        }
    }
};

}  // namespace

void runEarlyErrorPass(frontend::Node& program, SemaContext& context,
                       AnnotationStore& annotations) {
    Walker walker(annotations);
    walker.addAll(makeStrictnessVisitor());
    walker.addAll(makeControlFlowVisitor(annotations));
    walker.addAll(makeModuleVisitor(annotations));
    walker.addAll(std::make_unique<DuplicateProtoVisitor>());
    walker.walk(program, context);
}

}  // namespace neo::sema
