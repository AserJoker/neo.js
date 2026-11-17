// sema/EarlyErrorPass.cpp - 统一早期错误 walk 实现

#include "sema/EarlyErrorPass.h"

#include "regex/RegexEngine.h"
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

// 正则字面量：flags 与 pattern 必须在 Parse/Sema 阶段报 SyntaxError，
// 不能推迟到 push_regexp 执行期。
class RegExpLiteralVisitor final : public IVisitor {
public:
    void enter(Walker&, SemaContext& context, frontend::Node& node) override {
        if (node.kind() != frontend::NodeKind::RegExpLiteral) return;
        auto& literal = static_cast<frontend::RegExpLiteral&>(node);

        const regex::FlagsResult flags = regex::parseFlags(literal.flags);
        if (!flags.ok()) {
            context.error(
                shift(literal, 1 + literal.pattern.size() + 1,
                      flags.error ? flags.error->offset : 0),
                flags.error ? flags.error->message
                            : "Invalid regular expression flags");
            return;
        }

        const regex::CompileResult compiled =
            regex::compilePattern(literal.pattern, flags.flags);
        if (!compiled.ok()) {
            context.error(
                shift(literal, 1,
                      compiled.error ? compiled.error->offset : 0),
                compiled.error ? compiled.error->message
                               : "Invalid regular expression");
        }
    }

private:
    static frontend::SourceLocation shift(
        const frontend::RegExpLiteral& literal, std::size_t base,
        std::size_t delta) {
        frontend::SourceLocation location = literal.range().start;
        location.offset += base + delta;
        location.column += static_cast<std::uint32_t>(base + delta);
        return location;
    }
};

}  // namespace

void runEarlyErrorPass(frontend::Node& program, SemaContext& context,
                       AnnotationStore& annotations, bool inheritedStrict) {
    Walker walker(annotations);
    walker.addAll(makeStrictnessVisitor(inheritedStrict));
    walker.addAll(makeControlFlowVisitor(annotations));
    walker.addAll(makeModuleVisitor(annotations));
    walker.addAll(std::make_unique<DuplicateProtoVisitor>());
    walker.addAll(std::make_unique<RegExpLiteralVisitor>());
    walker.walk(program, context);
}

}  // namespace neo::sema
