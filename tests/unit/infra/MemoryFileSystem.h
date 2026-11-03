// memory_filesystem.h - 测试用内存虚拟文件系统（mock IFileSystem）
//
// 仅测试用，不入库生产代码。实现 IFileSystem 接口，用内存 stringstream 模拟文件，
// 无磁盘 I/O，可注入预置文件内容并断言写入结果。
//
// 回写机制：open(Write/Append) 时往 File::Impl 注入 on_close 闭包，
// close() 触发闭包把 ss 内容写回 fs->files_[path]，验证写入持久化。
// DiskFileSystem 不注入 on_close（真实 fstream 自行落盘）。
//
// 关联 ADR: 010（接口-实现-工厂，测试 mock）、027（可测试性）、028（infra）

#ifndef NEO_TEST_INFRA_MEMORY_FILESYSTEM_H
#define NEO_TEST_INFRA_MEMORY_FILESYSTEM_H

#include "infra/IFileSystem.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace neo::infra::test {

class MemoryFileSystem : public IFileSystem {
public:
    // 预置一个文件内容（测试 setup 用）
    void add_file(std::string_view path, std::string content) {
        files_[std::string(path)] = {std::move(content), true};
    }

    // 取出文件当前内容（测试断言用，对预置或 Write/Append close 后有效）
    std::string get_content(std::string_view path) const {
        auto it = files_.find(std::string(path));
        if (it == files_.end() || !it->second.exists) return "";
        return it->second.content;
    }

    Result<File> open(std::string_view path, OpenMode mode) override {
        std::string key(path);
        auto it = files_.find(key);
        bool exists = (it != files_.end() && it->second.exists);

        if (mode == OpenMode::Read || mode == OpenMode::ReadWrite) {
            if (!exists) return Result<File>::err(ErrCode::NotFound);
        }

        auto impl = std::make_unique<File::Impl>();
        impl->kind = File::Impl::Kind::Memory;

        if (exists && (mode == OpenMode::Read || mode == OpenMode::ReadWrite ||
                       mode == OpenMode::Append)) {
            impl->ss.str(it->second.content);
        }
        if (mode == OpenMode::Append) {
            impl->ss.clear();
            impl->ss.seekp(0, std::ios::end);
        }

        // Write/Append/ReadWrite 模式：close 时回写内容到 fs->files_，验证写入持久化。
        // 闭包捕获 this 和 path，从 ss 取 str 写回。Read 不回写（只读）。
        if (mode == OpenMode::Write || mode == OpenMode::Append ||
            mode == OpenMode::ReadWrite) {
            if (!exists) files_[key] = {"", true};
            impl->on_close = [this, key](std::string_view content) {
                files_[key].content = std::string(content);
                files_[key].exists  = true;
            };
        }

        return Result<File>::ok(File::make(std::move(impl)));
    }

    Result<void> remove(std::string_view path) override {
        auto it = files_.find(std::string(path));
        if (it == files_.end() || !it->second.exists) {
            return Result<void>::err(ErrCode::NotFound);
        }
        it->second.exists = false;
        return Result<void>::ok();
    }

    Result<bool> exists(std::string_view path) override {
        auto it = files_.find(std::string(path));
        return Result<bool>::ok(it != files_.end() && it->second.exists);
    }

private:
    struct Entry {
        std::string content;
        bool        exists = true;
    };
    std::unordered_map<std::string, Entry> files_;
};

}  // namespace neo::infra::test

#endif  // NEO_TEST_INFRA_MEMORY_FILESYSTEM_H
