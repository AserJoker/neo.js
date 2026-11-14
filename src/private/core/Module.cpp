// Module.cpp - ES Module 基础设施实现
//
// 模块 = root scope + 原型为 null 的导出对象，由 Context 以绝对路径为 key 持有。
// 模块 root scope 作为 global 的子作用域挂入作用域树，故其跟踪的 Value
// （含导出对象）从 global 树可达（ADR-044），生命周期由 Context 管理。
//
// import 的本质：惰性创建模块 → 若未加载则在其 root scope 下执行 loader →
// 返回导出对象。循环依赖时返回当前（部分）导出对象。
//
// 绑定模型（ADR-059）：导入是导出方槽位的只读别名；导出是槽位映射。
// native loader 需先提升建槽并 exportSlot，再 importModule，最后 initialize。
// 完整的 Module Instantiation / Linking 由 Compiler/宿主编排。
//
// 每个模块记录自己的 dirname；相对说明符（./ ../）以当前加载模块的 dirname 为基准。
//
// 关联 ADR: 011（Context 门面）、012（Scope 树）、044（可达性标记）、
//          059（模块绑定与槽位共享 import）

#include "core/Context.h"

#include "core/types/Array.h"
#include "core/types/ModuleNamespace.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "vm/Code.h"
#include "vm/VM.h"

#include <new>
#include <string>
#include <utility>

namespace neo::core {

// ModuleCompilation 持有 unique_ptr<vm::Code>，而 vm::Code 在公共头中只有
// 前向声明；特殊成员必须在包含完整类型的 TU 里定义。
ModuleCompilation::ModuleCompilation() = default;
ModuleCompilation::~ModuleCompilation() = default;
ModuleCompilation::ModuleCompilation(ModuleCompilation&&) noexcept = default;
ModuleCompilation& ModuleCompilation::operator=(ModuleCompilation&&) noexcept =
    default;

Module::Module() = default;
Module::~Module() = default;

namespace {

bool isAsciiAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// 相对模块说明符：以 ./ 或 ../ 开头（JS 中裸说明符不是相对路径）。
bool isRelativeSpecifier(std::string_view p) {
    if (p.empty() || p[0] != '.') return false;
    if (p.size() == 1) return true;
    if (p[1] == '/' || p[1] == '\\') return true;
    return p[1] == '.' && (p.size() == 2 || p[2] == '/' || p[2] == '\\');
}

// 规范化路径：折叠 . / .. 与重复分隔符，统一用 '/'。
std::string normalizePath(std::string_view path) {
    std::string prefix;
    if (path.size() >= 2 && path[1] == ':' && isAsciiAlpha(path[0])) {
        prefix = std::string(path.substr(0, 2)) + "/";
        path.remove_prefix(2);
    } else if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
        prefix = "/";
    }
    const bool absolute = !prefix.empty();

    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = i;
        while (j < path.size() && path[j] != '/' && path[j] != '\\') ++j;
        std::string_view seg = path.substr(i, j - i);
        if (seg == "..") {
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (!absolute) {
                parts.emplace_back("..");
            }
        } else if (!seg.empty() && seg != ".") {
            parts.emplace_back(seg);
        }
        if (j >= path.size()) break;
        i = j + 1;
    }

    std::string out = prefix;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (k != 0) out.push_back('/');
        out += parts[k];
    }
    return out;
}

// id 的目录部分。无分隔符返回空串（非绝对路径）。
std::string dirnameOf(std::string_view id) {
    size_t pos = id.find_last_of("/\\");
    if (pos == std::string_view::npos) return {};
    if (pos == 0) return "/";
    return std::string(id.substr(0, pos));
}

// 相对说明符按基准目录解析；绝对/裸说明符原样返回。
std::string resolveSpecifier(const std::vector<Module*>& stack,
                             std::string_view specifier) {
    if (!isRelativeSpecifier(specifier) || stack.empty()) {
        return std::string(specifier);
    }
    std::string joined = stack.back()->dirname;
    if (!joined.empty()) joined.push_back('/');
    joined += std::string(specifier);
    return normalizePath(joined);
}

}  // namespace

bool Context::hasModule(std::string_view id) const {
    return modules_.find(std::string(id)) != modules_.end();
}

ModuleInfo Context::getModule(std::string_view id) {
    auto it = modules_.find(std::string(id));
    if (it != modules_.end()) {
        return ModuleInfo{it->second->scope, it->second->exports};
    }

    // 惰性创建模块 root scope（global 的子作用域，生命周期随作用域树）。
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return ModuleInfo{};
    Scope* scope = new (p) Scope(global_, alloc_, this);
    global_->addChild(scope);

    // 导出对象必须在模块 scope 下创建，才能被作用域树跟踪。
    // 它是模块命名空间 exotic 对象（原型 null、导出属性为活绑定）。
    Scope* savedCurrent = current_;
    current_ = scope;
    Value* exports = createModuleNamespace();
    current_ = savedCurrent;

    if (exports == oomValue_) {
        // 创建失败：销毁刚建的作用域（从 global 摘除并释放内存）。
        scope->destroySelf();
        return ModuleInfo{};
    }

    auto mod = std::make_unique<Module>();
    mod->id = std::string(id);
    mod->dirname = dirnameOf(id);
    mod->scope = scope;
    mod->exports = exports;
    ModuleInfo info{scope, exports};
    modules_.emplace(std::string(id), std::move(mod));
    return info;
}

void Context::setModuleLoader(std::string_view id, ModuleLoader loader) {
    ModuleInfo info = getModule(id);
    if (info.scope == nullptr) return;
    auto it = modules_.find(std::string(id));
    if (it != modules_.end()) it->second->loader = loader;
}

Module* Context::findModule(std::string_view id) {
    ModuleInfo info = getModule(id);
    if (info.scope == nullptr) return nullptr;
    auto it = modules_.find(std::string(id));
    return it == modules_.end() ? nullptr : it->second.get();
}

bool Context::setModuleCompilation(std::string_view id,
                                   ModuleCompilation compilation) {
    Module* mod = findModule(id);
    if (mod == nullptr) return false;
    mod->compilation = std::move(compilation);
    mod->declared = false;
    mod->declaring = false;
    mod->instantiated = false;
    mod->instantiating = false;
    mod->evaluated = false;
    mod->loaded = false;
    return true;
}

Value* Context::runModuleCode(Module* mod, uint32_t entryPc) {
    if (mod->compilation.code == nullptr) return nullptr;
    Scope* savedRoot = root_;
    Scope* savedCurrent = current_;
    root_ = mod->scope;
    current_ = mod->scope;
    vm::VM machine(this, mod->compilation.code.get());
    Value* res = machine.run(entryPc);
    root_ = savedRoot;
    current_ = savedCurrent;
    return res;
}

Value* Context::declareModule(Module* mod) {
    if (mod->declared) return nullptr;
    // 循环依赖：依赖方槽位尚未建立，先返回让其继续声明。
    if (mod->declaring) return nullptr;

    mod->declaring = true;
    moduleStack_.push_back(mod);

    Scope* savedRoot = root_;
    Scope* savedCurrent = current_;
    root_ = mod->scope;
    current_ = mod->scope;
    auto fail = [&](Value* error) -> Value* {
        moduleStack_.pop_back();
        mod->declaring = false;
        root_ = savedRoot;
        current_ = savedCurrent;
        return error;
    };

    // 1. 符号定义区：建槽。
    Value* res = runModuleCode(mod, mod->compilation.declarePc);
    if (res != nullptr && detail::isErrorValue(res)) return fail(res);

    // 2. 本地绑定导出：槽位已存在，先建立映射（循环依赖时依赖方即可解析）。
    //    导出名指向 import 别名的条目此时还无法解析，留到 instantiate 重试。
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (!exp.specifier.empty() || exp.exportAll) continue;
        if (current()->lookupBindingChain(exp.localName) == nullptr) continue;
        Value* exportRes =
            exportBinding(mod->exports, exp.exportName, exp.localName);
        if (detail::isErrorValue(exportRes)) return fail(exportRes);
    }

    // 3. 递归声明依赖（import 与 re-export 的源模块）。
    for (const ModuleImportBinding& imp : mod->compilation.imports) {
        Module* dep = findModule(resolveSpecifier(moduleStack_, imp.specifier));
        if (dep == nullptr) return fail(oomValue_);
        Value* depRes = declareModule(dep);
        if (depRes != nullptr && detail::isErrorValue(depRes)) {
            return fail(depRes);
        }
    }
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (exp.specifier.empty()) continue;
        Module* dep = findModule(resolveSpecifier(moduleStack_, exp.specifier));
        if (dep == nullptr) return fail(oomValue_);
        Value* depRes = declareModule(dep);
        if (depRes != nullptr && detail::isErrorValue(depRes)) {
            return fail(depRes);
        }
    }

    moduleStack_.pop_back();
    mod->declaring = false;
    root_ = savedRoot;
    current_ = savedCurrent;
    mod->declared = true;
    return nullptr;
}

Value* Context::instantiateModule(Module* mod) {
    if (mod->instantiated) return nullptr;
    // 循环依赖：依赖方的 import/export 尚未建立，先返回让其继续实例化。
    if (mod->instantiating) return nullptr;

    Value* declared = declareModule(mod);
    if (declared != nullptr && detail::isErrorValue(declared)) return declared;

    mod->instantiating = true;
    moduleStack_.push_back(mod);

    // link 期需要 current 指向模块作用域（import 绑定与 export 槽位查找）。
    Scope* savedRoot = root_;
    Scope* savedCurrent = current_;
    root_ = mod->scope;
    current_ = mod->scope;
    auto fail = [&](Value* error) -> Value* {
        moduleStack_.pop_back();
        mod->instantiating = false;
        root_ = savedRoot;
        current_ = savedCurrent;
        return error;
    };

    // 1. import：依赖已在 declare 阶段建好本地导出槽位，这里建立共享别名。
    for (const ModuleImportBinding& imp : mod->compilation.imports) {
        std::string depId = resolveSpecifier(moduleStack_, imp.specifier);
        Module* dep = findModule(depId);
        if (dep == nullptr) return fail(oomValue_);
        Value* depRes = instantiateModule(dep);
        if (depRes != nullptr && detail::isErrorValue(depRes)) {
            return fail(depRes);
        }
        Value* bindRes =
            imp.namespaceImport
                ? bindImportSlot(imp.localName, dep->exports)
                : bindImport(imp.localName, dep->exports, imp.importName);
        if (bindRes != nullptr && detail::isErrorValue(bindRes)) {
            return fail(bindRes);
        }
    }

    // 2. 本地导出重试：声明阶段无法解析的 import 别名现在可以建立。
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (!exp.specifier.empty() || exp.exportAll) continue;
        if (findExportSlot(mod->exports, exp.exportName) != nullptr) continue;
        Value* exportRes =
            exportBinding(mod->exports, exp.exportName, exp.localName);
        if (detail::isErrorValue(exportRes)) return fail(exportRes);
    }

    // 3. re-export / export *：依赖的导出表此时已建立。
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (exp.exportAll) {
            std::string depId = resolveSpecifier(moduleStack_, exp.specifier);
            Module* dep = findModule(depId);
            if (dep == nullptr) return fail(oomValue_);
            Value* depRes = instantiateModule(dep);
            if (depRes != nullptr && detail::isErrorValue(depRes)) {
                return fail(depRes);
            }
            Value* keys = ownPropertyKeys(dep->exports, true, false);
            if (detail::isErrorValue(keys)) return fail(keys);
            auto* elements = static_cast<ArrayData*>(keys->data());
            for (uint32_t i = 0; i < elements->length(); ++i) {
                Value* key = elements->elementAt(i);
                if (key == nullptr || key->type()->kind() != TypeKind::String) {
                    continue;
                }
                std::string name =
                    static_cast<StringData*>(key->data())->value;
                if (name == "default") continue;
                Value* slot = findExportSlot(dep->exports, name);
                if (slot == nullptr) continue;
                Value* exportRes = exportSlot(mod->exports, name, slot);
                if (detail::isErrorValue(exportRes)) return fail(exportRes);
            }
            continue;
        }

        if (exp.specifier.empty()) continue;
        std::string depId = resolveSpecifier(moduleStack_, exp.specifier);
        Module* dep = findModule(depId);
        if (dep == nullptr) return fail(oomValue_);
        Value* depRes = instantiateModule(dep);
        if (depRes != nullptr && detail::isErrorValue(depRes)) {
            return fail(depRes);
        }
        // importName 为空：export * as ns，导出命名空间对象本身。
        Value* slot = exp.importName.empty()
                          ? dep->exports
                          : getExportSlot(dep->exports, exp.importName);
        if (detail::isErrorValue(slot)) return fail(slot);
        Value* exportRes =
            exportSlot(mod->exports, exp.exportName, slot);
        if (detail::isErrorValue(exportRes)) return fail(exportRes);
    }

    // 4. 函数定义区：import 已绑定，set_closure 可解析。
    Value* res = runModuleCode(mod, mod->compilation.instantiatePc);
    if (res != nullptr && detail::isErrorValue(res)) return fail(res);

    moduleStack_.pop_back();
    mod->instantiating = false;
    root_ = savedRoot;
    current_ = savedCurrent;
    mod->instantiated = true;
    return nullptr;
}

Value* Context::evaluateModule(Module* mod) {
    if (mod->evaluated || mod->loading) return nullptr;
    mod->loading = true;
    moduleStack_.push_back(mod);

    // 依赖 DFS 后序：先求值所有 import 与 re-export 的源模块。
    auto evaluateDependency = [this](const std::string& specifier) -> Value* {
        std::string depId = resolveSpecifier(moduleStack_, specifier);
        Module* dep = findModule(depId);
        if (dep == nullptr) return oomValue_;
        if (dep->compilation.code != nullptr) {
            Value* res = instantiateModule(dep);
            if (res != nullptr && detail::isErrorValue(res)) return res;
            return evaluateModule(dep);
        }
        // 原生 loader 模块：沿用惰性执行路径。
        return importModule(specifier);
    };

    for (const ModuleImportBinding& imp : mod->compilation.imports) {
        Value* res = evaluateDependency(imp.specifier);
        if (res != nullptr && detail::isErrorValue(res)) {
            moduleStack_.pop_back();
            mod->loading = false;
            return res;
        }
    }
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (exp.specifier.empty()) continue;
        Value* res = evaluateDependency(exp.specifier);
        if (res != nullptr && detail::isErrorValue(res)) {
            moduleStack_.pop_back();
            mod->loading = false;
            return res;
        }
    }

    Value* res = runModuleCode(mod, mod->compilation.evaluatePc);
    moduleStack_.pop_back();
    mod->loading = false;
    if (res != nullptr && detail::isErrorValue(res)) return res;
    mod->evaluated = true;
    mod->loaded = true;
    return nullptr;
}

Value* Context::importModule(std::string_view specifier) {
    // 相对说明符以「当前正在加载的模块」的目录为基准解析。
    std::string id = resolveSpecifier(moduleStack_, specifier);
    ModuleInfo info = getModule(id);
    if (info.scope == nullptr) return oomValue_;

    Module* mod = modules_.find(id)->second.get();

    // 编译模块：实例化 → 链接 → 求值（循环依赖由 loading 标记短路）。
    if (mod->compilation.code != nullptr) {
        if (mod->evaluated) return mod->exports;
        Value* err = instantiateModule(mod);
        if (err != nullptr && detail::isErrorValue(err)) return err;
        err = evaluateModule(mod);
        if (err != nullptr && detail::isErrorValue(err)) return err;
        return mod->exports;
    }

    // 已加载 / 正在加载（循环依赖）/ 无 loader：直接返回当前导出对象。
    if (mod->loaded || mod->loading || mod->loader == nullptr) {
        return mod->exports;
    }

    mod->loading = true;
    Scope* savedRoot = root_;
    Scope* savedCurrent = current_;
    root_ = mod->scope;
    current_ = mod->scope;
    moduleStack_.push_back(mod);
    Value* result = mod->loader(this, mod->exports);
    moduleStack_.pop_back();
    root_ = savedRoot;
    current_ = savedCurrent;
    mod->loading = false;

    // 失败：保持未加载（允许重试），原样透传错误值。
    if (result != nullptr && detail::isErrorValue(result)) return result;
    mod->loaded = true;
    return mod->exports;
}

Value* Context::exportSlot(Value* exports, std::string_view exportName,
                           Value* slot) {
    if (detail::isErrorValue(exports)) return exports;
    if (exports->type()->kind() != TypeKind::ModuleNamespace) {
        return detail::typeError(this, "export target is not a module namespace");
    }
    if (slot == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(exportName) + "' has no binding");
    }
    // ModuleNamespaceType::defineProperty 直接存槽位 Value*，不 clone。
    return defineProperty(exports, exportName, slot, false, true, false);
}

Value* Context::getExportSlot(Value* exports, std::string_view exportName) {
    if (detail::isErrorValue(exports)) return exports;
    Value* slot = findExportSlot(exports, exportName);
    if (slot == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(exportName) + "' is not defined");
    }
    return slot;
}

Value* Context::bindImportSlot(std::string_view localName, Value* slot) {
    if (detail::isErrorValue(slot)) return slot;
    if (slot == nullptr) {
        return detail::referenceError(
            this, "import binding '" + std::string(localName) + "' has no slot");
    }
    if (!current()->defineImport(localName, slot)) {
        return detail::errorOf(this, ErrorKind::SyntaxError,
                               "Identifier '" + std::string(localName) +
                                   "' has already been declared");
    }
    // 返回槽位当前值；未初始化时返回 undefined（读取仍由 load 抛 TDZ）。
    return slot->type()->kind() == TypeKind::Uninitialized ? createUndefined()
                                                           : slot;
}

Value* Context::exportBinding(Value* exports, std::string_view exportName,
                              std::string_view localName) {
    if (detail::isErrorValue(exports)) return exports;
    if (exports->type()->kind() != TypeKind::ModuleNamespace) {
        return detail::typeError(this, "export target is not a module namespace");
    }
    // re-export：localName 是导入绑定时，导出直接指向同一个源槽位。
    Value* slot = nullptr;
    if (!current()->getImportBindingChain(localName, slot)) {
        // 本地导出：直接取模块作用域里的绑定槽位。
        const Scope::Binding* binding =
            current()->lookupBindingChain(localName);
        if (binding != nullptr) slot = binding->value;
    }
    if (slot == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(localName) + "' is not defined");
    }
    return exportSlot(exports, exportName, slot);
}

Value* Context::bindImport(std::string_view localName, Value* namespaceObj,
                           std::string_view exportName) {
    if (detail::isErrorValue(namespaceObj)) return namespaceObj;
    if (namespaceObj->type()->kind() != TypeKind::ModuleNamespace) {
        return detail::typeError(this, "import target is not a module namespace");
    }
    Value* slot = findExportSlot(namespaceObj, exportName);
    if (slot == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(exportName) + "' is not defined");
    }
    return bindImportSlot(localName, slot);
}

}  // namespace neo::core
