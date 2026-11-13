// sema/ModulePass.cpp - 模块绑定与导出早期错误实现

#include "sema/ModulePass.h"

#include "sema/BindingNames.h"
#include "sema/ResolvePass.h"
#include "sema/Walker.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace neo::sema {

namespace {

using frontend::ClassDeclaration;
using frontend::ExportAllDeclaration;
using frontend::ExportNamedDeclaration;
using frontend::ExportSpecifier;
using frontend::FunctionDeclaration;
using frontend::Identifier;
using frontend::ImportDeclaration;
using frontend::ImportDefaultSpecifier;
using frontend::ImportNamespaceSpecifier;
using frontend::ImportSpecifier;
using frontend::MetaProperty;
using frontend::Node;
using frontend::NodeKind;
using frontend::NodePtr;
using frontend::Program;
using frontend::SourceRange;
using frontend::SourceType;
using frontend::StringLiteral;
using frontend::VariableDeclaration;
using frontend::VariableDeclarator;

bool isIdentifier(const Node* node, std::string_view name) {
    if (!node || node->kind() != NodeKind::Identifier) return false;
    return static_cast<const Identifier&>(*node).name == name;
}

std::string moduleExportName(const Node* node) {
    if (!node) return {};
    if (node->kind() == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*node).name;
    }
    if (node->kind() == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*node).value;
    }
    return {};
}

class ModuleVisitor final : public IVisitor {
public:
    explicit ModuleVisitor(AnnotationStore& annotations)
        : annotations_(annotations) {}

    void enter(Walker& /*walker*/, SemaContext& context,
               Node& node) override {
        switch (node.kind()) {
            case NodeKind::Program: {
                auto& program = static_cast<Program&>(node);
                module_ = program.sourceType == SourceType::Module;
                break;
            }
            case NodeKind::ImportDeclaration:
                enterImport(node, context);
                break;
            case NodeKind::ExportNamedDeclaration:
                enterExportNamed(node, context);
                break;
            case NodeKind::ExportDefaultDeclaration:
                enterExportDefault(node, context);
                break;
            case NodeKind::ExportAllDeclaration:
                enterExportAll(node, context);
                break;
            case NodeKind::MetaProperty:
                checkImportMeta(node, context);
                break;
            default:
                break;
        }
    }

private:
    void enterImport(Node& node, SemaContext& context) {
        if (!module_) {
            context.error(node.range().start,
                          "import declarations are only allowed in modules");
        }

        auto& declaration = static_cast<ImportDeclaration&>(node);
        for (NodePtr& specifier : declaration.specifiers) {
            if (!specifier) continue;
            Node* local = nullptr;
            switch (specifier->kind()) {
                case NodeKind::ImportSpecifier:
                    local =
                        static_cast<ImportSpecifier&>(*specifier).local.get();
                    break;
                case NodeKind::ImportDefaultSpecifier:
                    local = static_cast<ImportDefaultSpecifier&>(*specifier)
                                .local.get();
                    break;
                case NodeKind::ImportNamespaceSpecifier:
                    local = static_cast<ImportNamespaceSpecifier&>(*specifier)
                                .local.get();
                    break;
                default:
                    continue;
            }
            if (local && local->kind() != NodeKind::Identifier) {
                context.error(local->range().start,
                              "Import binding must be an identifier");
            }
        }
    }

    void enterExportNamed(Node& node, SemaContext& context) {
        if (!module_) {
            context.error(node.range().start,
                          "export declarations are only allowed in modules");
        }

        auto& declaration = static_cast<ExportNamedDeclaration&>(node);
        if (declaration.declaration) {
            collectDeclarationExports(*declaration.declaration, context);
        }

        for (NodePtr& specifier : declaration.specifiers) {
            if (!specifier ||
                specifier->kind() != NodeKind::ExportSpecifier) {
                continue;
            }
            auto& entry = static_cast<ExportSpecifier&>(*specifier);
            const std::string name = moduleExportName(entry.exported.get());
            if (name.empty()) {
                context.error(entry.range().start, "Invalid export name");
                continue;
            }
            addExport(name,
                      entry.exported ? entry.exported->range()
                                     : entry.range(),
                      context);
            if (!declaration.source) {
                checkLocalExport(entry, context);
            }
        }
    }

    void enterExportDefault(Node& node, SemaContext& context) {
        if (!module_) {
            context.error(node.range().start,
                          "export declarations are only allowed in modules");
        }
        addExport("default", node.range(), context);
    }

    void enterExportAll(Node& node, SemaContext& context) {
        if (!module_) {
            context.error(node.range().start,
                          "export declarations are only allowed in modules");
        }

        auto& declaration = static_cast<ExportAllDeclaration&>(node);
        if (!declaration.exported) return;

        const std::string name = moduleExportName(declaration.exported.get());
        if (name.empty()) {
            context.error(declaration.range().start, "Invalid export name");
            return;
        }
        addExport(name, declaration.exported->range(), context);
    }

    void checkImportMeta(Node& node, SemaContext& context) {
        auto& property = static_cast<MetaProperty&>(node);
        if (!isIdentifier(property.meta.get(), "import") ||
            !isIdentifier(property.property.get(), "meta")) {
            return;
        }
        if (!module_) {
            context.error(node.range().start,
                          "import.meta is only allowed in modules");
        }
    }

    void collectDeclarationExports(Node& declaration, SemaContext& context) {
        switch (declaration.kind()) {
            case NodeKind::VariableDeclaration: {
                auto& variables =
                    static_cast<VariableDeclaration&>(declaration);
                for (NodePtr& declarator : variables.declarations) {
                    if (!declarator) continue;
                    auto& entry =
                        static_cast<VariableDeclarator&>(*declarator);
                    std::vector<BindingName> names;
                    bool nonSimple = false;
                    collectBindingNames(entry.id.get(), names, nonSimple);
                    for (const BindingName& name : names) {
                        addExport(name.name, name.range, context);
                    }
                }
                break;
            }
            case NodeKind::FunctionDeclaration: {
                auto& function =
                    static_cast<FunctionDeclaration&>(declaration);
                addDeclarationName(function.id.get(), context);
                break;
            }
            case NodeKind::ClassDeclaration: {
                auto& classDeclaration =
                    static_cast<ClassDeclaration&>(declaration);
                addDeclarationName(classDeclaration.id.get(), context);
                break;
            }
            default:
                break;
        }
    }

    void addDeclarationName(const Node* id, SemaContext& context) {
        if (!id || id->kind() != NodeKind::Identifier) return;
        const auto& identifier = static_cast<const Identifier&>(*id);
        addExport(identifier.name, identifier.range(), context);
    }

    void checkLocalExport(ExportSpecifier& specifier, SemaContext& context) {
        Node* local = specifier.local.get();
        if (!local) return;

        if (local->kind() != NodeKind::Identifier) {
            context.error(local->range().start,
                          "A string literal cannot be used as an exported "
                          "binding without 'from'");
            return;
        }

        const auto* binding =
            annotations_.get<IdentifierBinding>(local->id());
        if (binding == nullptr || binding->binding == nullptr) {
            const auto& identifier = static_cast<Identifier&>(*local);
            context.error(local->range().start,
                          "Export '" + identifier.name + "' is not defined");
        }
    }

    void addExport(const std::string& name, SourceRange range,
                   SemaContext& context) {
        if (name.empty()) return;
        if (!exported_.insert(name).second) {
            context.error(range.start,
                          "Duplicate export of '" + name + "'");
        }
    }

    AnnotationStore& annotations_;
    bool module_ = false;
    std::unordered_set<std::string> exported_;
};

}  // namespace

void runModulePass(frontend::Node& program, SemaContext& context,
                   AnnotationStore& annotations) {
    Walker walker(annotations);
    walker.addAll(std::make_unique<ModuleVisitor>(annotations));
    walker.walk(program, context);
}

}  // namespace neo::sema
