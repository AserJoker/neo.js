// Frontmatter.cpp - Test262 元数据解析实现

#include "Frontmatter.h"

#include <algorithm>
#include <cctype>

namespace neo::test262 {

namespace {

std::string_view trimView(std::string_view text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string unquote(std::string_view text) {
    text = trimView(text);
    if (text.size() >= 2) {
        const char first = text.front();
        const char last = text.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            text.remove_prefix(1);
            text.remove_suffix(1);
        }
    }
    return std::string(text);
}

// 行内数组：[a, b, c]（Test262 不使用嵌套结构）。
std::vector<std::string> parseInlineList(std::string_view value) {
    std::vector<std::string> out;
    value = trimView(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        return out;
    }
    value.remove_prefix(1);
    value.remove_suffix(1);
    size_t start = 0;
    for (size_t i = 0; i <= value.size(); ++i) {
        if (i == value.size() || value[i] == ',') {
            std::string item = unquote(value.substr(start, i - start));
            if (!item.empty()) out.push_back(std::move(item));
            start = i + 1;
        }
    }
    return out;
}

bool isListKey(std::string_view key) {
    return key == "includes" || key == "flags" || key == "features";
}

}  // namespace

bool Metadata::hasFlag(std::string_view flag) const {
    return std::find(flags.begin(), flags.end(), flag) != flags.end();
}

bool Metadata::hasFeature(std::string_view feature) const {
    return std::find(features.begin(), features.end(), feature) !=
           features.end();
}

Metadata parseMetadata(std::string_view source) {
    Metadata meta;
    const size_t begin = source.find("/*---");
    if (begin == std::string_view::npos) return meta;
    const size_t bodyBegin = begin + 5;
    const size_t end = source.find("---*/", bodyBegin);
    if (end == std::string_view::npos) return meta;

    std::string_view body = source.substr(bodyBegin, end - bodyBegin);
    std::string currentKey;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t lineEnd = body.find('\n', pos);
        if (lineEnd == std::string_view::npos) lineEnd = body.size();
        std::string_view line = body.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;

        std::string_view trimmed = trimView(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;

        // 块列表项：key 已在前一行确定。
        if (trimmed.size() >= 2 && trimmed[0] == '-' &&
            std::isspace(static_cast<unsigned char>(trimmed[1])) != 0) {
            if (isListKey(currentKey)) {
                std::string item = unquote(trimmed.substr(1));
                if (!item.empty()) {
                    if (currentKey == "includes") meta.includes.push_back(item);
                    else if (currentKey == "flags") meta.flags.push_back(item);
                    else if (currentKey == "features") meta.features.push_back(item);
                }
            }
            continue;
        }

        const size_t colon = trimmed.find(':');
        if (colon == std::string_view::npos) {
            // info/description 的块正文，忽略。
            continue;
        }
        std::string key(trimView(trimmed.substr(0, colon)));
        std::string_view value = trimView(trimmed.substr(colon + 1));

        if (key == "negative") {
            meta.hasNegative = true;
            currentKey = "negative";
            continue;
        }
        if (currentKey == "negative" && (key == "phase" || key == "type")) {
            if (key == "phase") meta.negative.phase = unquote(value);
            else meta.negative.type = unquote(value);
            continue;
        }
        if (isListKey(key)) {
            currentKey = key;
            if (!value.empty()) {
                std::vector<std::string> items = parseInlineList(value);
                if (key == "includes") {
                    meta.includes.insert(meta.includes.end(), items.begin(),
                                         items.end());
                } else if (key == "flags") {
                    meta.flags.insert(meta.flags.end(), items.begin(),
                                      items.end());
                } else {
                    meta.features.insert(meta.features.end(), items.begin(),
                                         items.end());
                }
            }
            continue;
        }
        // 其他键（description/esid/info/...）不参与执行。
        currentKey.clear();
    }
    return meta;
}

}  // namespace neo::test262
