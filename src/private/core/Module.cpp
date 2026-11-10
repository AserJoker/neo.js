// Module.cpp - ES Module 基础设施实现
//
// 模块 = root scope + 原型为 null 的导出对象，由 Context 以绝对路径为 key 持有。
// 模块 root scope 作为 global 的子作用域挂入作用域树，故其跟踪的 Value
// （含导出对象）从 global 树可达（ADR-044），生命周期由 Context 管理。
//
// import 的本质：惰性创建模块 → 若未加载则在其 root scope 下执行 loader →
// 返回导出对象。循环依赖时返回当前（部分）导出对象。
// 每个模块记录自己的 dirname；相对说明符（./ ../）以当前加载模块的 dirname 为基准。
//
// 关联 ADR: 011（Context 门面）、012（Scope 树）、044（可达性标记）

#include "core/Context.h"

#include "core/types/TypeBehavior.h"

#include <new>
#include <string>
#include <utility>

namespace neo::core {

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
    Scope* scope = new (p) Scope(global_, alloc_);
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

Value* Context::importModule(std::string_view specifier) {
    // 相对说明符以「当前正在加载的模块」的目录为基准解析。
    std::string id = resolveSpecifier(moduleStack_, specifier);
    ModuleInfo info = getModule(id);
    if (info.scope == nullptr) return oomValue_;

    Module* mod = modules_.find(id)->second.get();
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

Value* Context::exportBinding(Value* exports, std::string_view exportName,
                              std::string_view localName) {
    if (exports->type()->kind() != TypeKind::ModuleNamespace) {
        return detail::typeError(this, "export target is not a module namespace");
    }
    // 活绑定：直接取模块作用域里的槽位 Value*（顶层绑定由模块 scope 持有）。
    Value* binding = current()->lookupChain(localName);
    if (binding == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(localName) + "' is not defined");
    }
    // ModuleNamespaceType::defineProperty 直接存 Value*，不 clone。
    return defineProperty(exports, exportName, binding, false, true, false);
}

}  // namespace neo::core
