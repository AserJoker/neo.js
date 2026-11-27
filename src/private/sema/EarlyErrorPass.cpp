// sema/EarlyErrorPass.cpp - 统一早期错误 walk 实现

#include "sema/EarlyErrorPass.h"

#include "regex/RegexEngine.h"
#include "sema/BindingNames.h"
#include "sema/ControlFlowPass.h"
#include "sema/ModulePass.h"
#include "sema/StrictnessPass.h"
#include "sema/Walker.h"

#include <map>
#include <string>
#include <vector>

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
                prop.propertyKind != frontend::PropertyKind::Init || !prop.key) {
                continue;
            }
            // 标识符键与字符串字面量键都算 PropertyName；计算键不算。
            std::string name;
            if (prop.key->kind() == frontend::NodeKind::Identifier) {
                name =
                    static_cast<const frontend::Identifier&>(*prop.key).name;
            } else if (prop.key->kind() == frontend::NodeKind::StringLiteral) {
                name = static_cast<const frontend::StringLiteral&>(*prop.key)
                           .value;
            } else {
                continue;
            }
            if (name != "__proto__") continue;
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

// CoverInitializedName（ES2024 §13.2.5.1）：`{ a = 1 }` 只作为对象赋值模式
// 的 cover grammar 存在。Parser 在确定为赋值/绑定模式时会把 ObjectExpression
// 转换为 ObjectPattern；残留到 Sema 的 ObjectExpression 中的简写初始化器
// 必然未转换为模式，属早期错误。
class CoverInitializedNameVisitor final : public IVisitor {
public:
    void enter(Walker&, SemaContext& context, frontend::Node& node) override {
        if (node.kind() != frontend::NodeKind::ObjectExpression) return;
        auto& object = static_cast<frontend::ObjectExpression&>(node);
        for (const frontend::NodePtr& property : object.properties) {
            if (!property || property->kind() != frontend::NodeKind::Property) {
                continue;
            }
            const auto& prop = static_cast<const frontend::Property&>(*property);
            if (!prop.shorthand || !prop.value) continue;
            if (prop.value->kind() != frontend::NodeKind::AssignmentExpression) {
                continue;
            }
            context.error(prop.value->range().start,
                          "Invalid shorthand property initializer");
            return;
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

// 类元素早期错误（ES2024 §15.7.1 / §15.7.10）：
//   - 字段名不得为 "constructor"；静态字段名不得为 "prototype"/"constructor"
//   - 静态方法名不得为 "prototype"
//   - 非静态 "constructor" 只能是普通方法，且至多一个
//   - 私有名不得为 "constructor"，且同一类内不得重名
//     （getter/setter 成对且静态性一致时除外）
class ClassElementVisitor final : public IVisitor {
public:
    void enter(Walker&, SemaContext& context, frontend::Node& node) override {
        if (node.kind() != frontend::NodeKind::ClassBody) return;
        auto& body = static_cast<frontend::ClassBody&>(node);

        bool hasConstructor = false;
        std::map<std::string, std::vector<PrivateEntry>> privates;

        for (const frontend::NodePtr& element : body.elements) {
            if (!element) continue;
            const bool isField =
                element->kind() == frontend::NodeKind::PropertyDefinition;
            const bool isMethod =
                element->kind() == frontend::NodeKind::MethodDefinition;
            if (!isField && !isMethod) continue;

            frontend::NodePtr* keyPtr = nullptr;
            bool computed = false;
            bool isStatic = false;
            frontend::MethodKind methodKind = frontend::MethodKind::Method;
            if (isField) {
                auto& field = static_cast<frontend::PropertyDefinition&>(*element);
                keyPtr = &field.key;
                computed = field.computed;
                isStatic = field.isStatic;
            } else {
                auto& method = static_cast<frontend::MethodDefinition&>(*element);
                keyPtr = &method.key;
                computed = method.computed;
                isStatic = method.isStatic;
                methodKind = method.methodKind;
            }
            if (!keyPtr || !*keyPtr || computed) continue;

            std::string name;
            bool isPrivate = false;
            if (!propName(**keyPtr, name, isPrivate)) continue;
            const frontend::SourceLocation location = (*keyPtr)->range().start;

            if (isPrivate) {
                if (name == "constructor") {
                    context.error(location,
                                  "Classes may not have a private name 'constructor'");
                    continue;
                }
                PrivateEntry entry{isStatic, isField, methodKind};
                auto& entries = privates[name];
                bool ok = true;
                for (const PrivateEntry& existing : entries) {
                    if (!accessorPair(existing, entry)) {
                        ok = false;
                        break;
                    }
                }
                entries.push_back(entry);
                if (!ok) {
                    context.error(location,
                                  "Duplicate private name '#" + name + "'");
                }
                continue;
            }

            if (isField) {
                if (name == "constructor") {
                    context.error(location,
                                  "Classes may not have a field named 'constructor'");
                } else if (isStatic && name == "prototype") {
                    context.error(location,
                                  "Classes may not have a static field named 'prototype'");
                }
                continue;
            }

            if (isStatic) {
                if (name == "prototype") {
                    context.error(location,
                                  "Classes may not have a static method named 'prototype'");
                }
                continue;
            }
            if (name != "constructor") continue;
            if (methodKind != frontend::MethodKind::Constructor) {
                context.error(location,
                              "Class constructor may not be an accessor, async, or generator method");
                continue;
            }
            if (hasConstructor) {
                context.error(location,
                              "A class may only have one constructor");
            }
            hasConstructor = true;
        }
    }

private:
    struct PrivateEntry {
        bool isStatic;
        bool isField;
        frontend::MethodKind kind;
    };

    static bool accessorPair(const PrivateEntry& a, const PrivateEntry& b) {
        if (a.isField || b.isField || a.isStatic != b.isStatic) return false;
        return (a.kind == frontend::MethodKind::Get &&
                b.kind == frontend::MethodKind::Set) ||
               (a.kind == frontend::MethodKind::Set &&
                b.kind == frontend::MethodKind::Get);
    }

    static bool propName(const frontend::Node& key, std::string& out,
                         bool& isPrivate) {
        switch (key.kind()) {
            case frontend::NodeKind::Identifier:
                out = static_cast<const frontend::Identifier&>(key).name;
                isPrivate = false;
                return true;
            case frontend::NodeKind::StringLiteral:
                out = static_cast<const frontend::StringLiteral&>(key).value;
                isPrivate = false;
                return true;
            case frontend::NodeKind::PrivateIdentifier:
                out = static_cast<const frontend::PrivateIdentifier&>(key).name;
                isPrivate = true;
                return true;
            default:
                return false;
        }
    }
};

// 词法声明不得包含名为 "let" 的绑定（ES2024 §14.3.1.1 / §14.7.1）。
class LexicalBindingVisitor final : public IVisitor {
public:
    void enter(Walker&, SemaContext& context, frontend::Node& node) override {
        if (node.kind() != frontend::NodeKind::VariableDeclaration) return;
        auto& declaration =
            static_cast<frontend::VariableDeclaration&>(node);
        if (declaration.variableKind == frontend::VariableKind::Var) return;

        std::vector<BindingName> names;
        bool nonSimple = false;
        for (frontend::NodePtr& declarator : declaration.declarations) {
            if (!declarator) continue;
            auto& entry = static_cast<frontend::VariableDeclarator&>(*declarator);
            collectBindingNames(entry.id.get(), names, nonSimple);
        }
        for (const BindingName& name : names) {
            if (name.name == "let") {
                context.error(name.range.start,
                              "The declaration may not contain a binding for 'let'");
            }
        }
    }
};

// ClassStaticBlock 内（不跨函数/static block 边界）的 BindingIdentifier
// 不得为 "await"（ES2024 §15.7.1）。
class StaticBlockAwaitVisitor final : public IVisitor {
public:
    void enter(Walker&, SemaContext& context, frontend::Node& node) override {
        using frontend::NodeKind;
        switch (node.kind()) {
            case NodeKind::StaticBlock:
                ++staticDepth_;
                return;
            case NodeKind::FunctionDeclaration: {
                auto& function =
                    static_cast<frontend::FunctionDeclaration&>(node);
                checkName(context, function.id.get());
                ++functionDepth_;
                return;
            }
            case NodeKind::ArrowFunctionExpression: {
                // 箭头形参处于 +Await 上下文（ConciseBody 才是 ~Await），
                // 因此静态块的绑定限制作用于形参但不穿透函数体。
                auto& arrow =
                    static_cast<frontend::ArrowFunctionExpression&>(node);
                if (staticDepth_ != 0 && functionDepth_ == 0) {
                    for (frontend::NodePtr& param : arrow.params) {
                        reportAwait(context, param.get());
                    }
                }
                ++functionDepth_;
                return;
            }
            case NodeKind::FunctionExpression:
                ++functionDepth_;
                return;
            case NodeKind::ClassDeclaration: {
                auto& declaration =
                    static_cast<frontend::ClassDeclaration&>(node);
                checkName(context, declaration.id.get());
                return;
            }
            case NodeKind::CatchClause: {
                auto& clause = static_cast<frontend::CatchClause&>(node);
                checkName(context, clause.param.get());
                return;
            }
            case NodeKind::VariableDeclaration: {
                auto& declaration =
                    static_cast<frontend::VariableDeclaration&>(node);
                for (frontend::NodePtr& declarator : declaration.declarations) {
                    if (!declarator) continue;
                    auto& entry = static_cast<frontend::VariableDeclarator&>(
                        *declarator);
                    checkName(context, entry.id.get());
                }
                return;
            }
            default:
                return;
        }
    }

    void leave(Walker&, SemaContext&, frontend::Node& node) override {
        using frontend::NodeKind;
        if (node.kind() == NodeKind::StaticBlock) {
            --staticDepth_;
        } else if (node.kind() == NodeKind::FunctionDeclaration ||
                   node.kind() == NodeKind::FunctionExpression ||
                   node.kind() == NodeKind::ArrowFunctionExpression) {
            --functionDepth_;
        }
    }

private:
    static void reportAwait(SemaContext& context, frontend::Node* pattern) {
        if (pattern == nullptr) return;
        std::vector<BindingName> names;
        bool nonSimple = false;
        collectBindingNames(pattern, names, nonSimple);
        for (const BindingName& name : names) {
            if (name.name == "await") {
                context.error(name.range.start,
                              "BindingIdentifier may not be 'await' inside a "
                              "class static block");
            }
        }
    }

    // 只在 static block 内且未跨函数边界时检查。
    void checkName(SemaContext& context, frontend::Node* pattern) {
        if (staticDepth_ == 0 || functionDepth_ != 0) return;
        reportAwait(context, pattern);
    }

    int staticDepth_ = 0;
    int functionDepth_ = 0;
};

}  // namespace

void runEarlyErrorPass(frontend::Node& program, SemaContext& context,
                       AnnotationStore& annotations, const SemaOptions& options) {
    Walker walker(annotations);
    walker.addAll(makeStrictnessVisitor(options.inheritedStrict));
    walker.addAll(makeControlFlowVisitor(annotations, options));
    walker.addAll(makeModuleVisitor(annotations));
    walker.addAll(std::make_unique<DuplicateProtoVisitor>());
    walker.addAll(std::make_unique<CoverInitializedNameVisitor>());
    walker.addAll(std::make_unique<ClassElementVisitor>());
    walker.addAll(std::make_unique<LexicalBindingVisitor>());
    walker.addAll(std::make_unique<StaticBlockAwaitVisitor>());
    walker.addAll(std::make_unique<RegExpLiteralVisitor>());
    walker.walk(program, context);
}

}  // namespace neo::sema
