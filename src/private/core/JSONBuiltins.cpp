// JSONBuiltins.cpp - JSON.parse / JSON.stringify（ES2024 §25.5）
//
// 设计要点：
//   - 手写递归下降解析器，直接构造 Value*，不引入中间 DOM；
//   - 对象键用 CreateDataProperty（defineProperty），__proto__ 是自有属性；
//   - 重复键覆盖值但保留首次插入顺序；
//   - 数字用严格 JSON 文法 + std::from_chars（locale 无关）；
//   - \uXXXX/代理对按 UTF-8/WTF-8 处理，孤立代理可往返；
//   - reviver/replacer/toJSON 全部走内部方法，Proxy/getter 可观察；
//   - 序列化直接写字符串缓冲，不建中间结构。
//
// 关联 ADR: 020（标准库组织）、048（内部方法层）

#include "core/JSONBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/Proxy.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "unicode/Utf8.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo::core {

namespace {

constexpr int kMaxJsonDepth = 1000;

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// IsArray（ES2024 §7.2.2）：Proxy 递归穿透 target。
bool isArrayValue(Value* v) {
    while (v != nullptr && v->type()->kind() == TypeKind::Proxy) {
        v = proxyTarget(v);
    }
    return v != nullptr && v->type()->kind() == TypeKind::Array;
}

// —— 解析器 ——

class JsonParser {
public:
    JsonParser(IContext* ctx, std::string_view src) : ctx_(ctx), src_(src) {}

    Value* parse() {
        skipWhitespace();
        Value* v = parseValue(0);
        if (detail::isErrorValue(v)) return v;
        skipWhitespace();
        if (pos_ != src_.size()) return syntaxError("Unexpected token");
        return v;
    }

private:
    IContext* ctx_;
    std::string_view src_;
    size_t pos_ = 0;

    Value* syntaxError(const char* message) {
        std::string msg = message;
        msg += " at position ";
        msg += std::to_string(pos_);
        return ctx_->createErrorException(ErrorKind::SyntaxError, msg);
    }

    void skipWhitespace() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume(char c) {
        if (pos_ < src_.size() && src_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool match(const char* literal) {
        size_t len = std::strlen(literal);
        if (src_.substr(pos_, len) != literal) return false;
        pos_ += len;
        return true;
    }

    Value* parseValue(int depth) {
        if (depth > kMaxJsonDepth) {
            return ctx_->createErrorException(ErrorKind::RangeError,
                                              "JSON nesting is too deep");
        }
        skipWhitespace();
        if (pos_ >= src_.size()) {
            return syntaxError("Unexpected end of JSON input");
        }
        char c = src_[pos_];
        switch (c) {
            case '{':
                return parseObject(depth + 1);
            case '[':
                return parseArray(depth + 1);
            case '"':
                return parseString();
            case 't':
                if (!match("true")) return syntaxError("Unexpected token");
                return ctx_->createBoolean(true);
            case 'f':
                if (!match("false")) return syntaxError("Unexpected token");
                return ctx_->createBoolean(false);
            case 'n':
                if (!match("null")) return syntaxError("Unexpected token");
                return ctx_->createNull();
            default:
                if (c == '-' || isDigit(c)) return parseNumber();
                return syntaxError("Unexpected token");
        }
    }

    Value* parseObject(int depth) {
        consume('{');
        Value* obj = ctx_->createObject();
        if (obj == ctx_->oomValue()) return obj;
        skipWhitespace();
        if (consume('}')) return obj;

        while (true) {
            skipWhitespace();
            if (pos_ >= src_.size() || src_[pos_] != '"') {
                return syntaxError("Expected property name");
            }
            Value* key = parseString();
            if (detail::isErrorValue(key)) return key;
            skipWhitespace();
            if (!consume(':')) return syntaxError("Expected ':'");
            Value* value = parseValue(depth);
            if (detail::isErrorValue(value)) return value;
            // CreateDataProperty：__proto__ 成为自有数据属性；重复键覆盖保序。
            Value* r = ctx_->defineProperty(obj, key, value, true, true, true);
            if (detail::isErrorValue(r)) return r;
            skipWhitespace();
            if (consume(',')) continue;
            if (consume('}')) return obj;
            return syntaxError("Expected ',' or '}'");
        }
    }

    Value* parseArray(int depth) {
        consume('[');
        Value* arr = ctx_->createArray();
        if (arr == ctx_->oomValue()) return arr;
        skipWhitespace();
        if (consume(']')) return arr;

        uint32_t index = 0;
        while (true) {
            Value* value = parseValue(depth);
            if (detail::isErrorValue(value)) return value;
            Value* key = ctx_->createString(std::to_string(index));
            if (key == ctx_->oomValue()) return key;
            Value* r = ctx_->defineProperty(arr, key, value, true, true, true);
            if (detail::isErrorValue(r)) return r;
            ++index;
            skipWhitespace();
            if (consume(',')) continue;
            if (consume(']')) return arr;
            return syntaxError("Expected ',' or ']'");
        }
    }

    bool readHex4(uint32_t& out) {
        if (pos_ + 4 > src_.size()) return false;
        uint32_t value = 0;
        for (size_t i = 0; i < 4; ++i) {
            int d = hexValue(src_[pos_ + i]);
            if (d < 0) return false;
            value = (value << 4) | static_cast<uint32_t>(d);
        }
        pos_ += 4;
        out = value;
        return true;
    }

    Value* parseString() {
        if (!consume('"')) return syntaxError("Expected string");
        std::string out;
        while (true) {
            if (pos_ >= src_.size()) return syntaxError("Unterminated string");
            auto c = static_cast<unsigned char>(src_[pos_]);
            if (c == '"') {
                ++pos_;
                break;
            }
            if (c < 0x20) {
                return syntaxError("Unescaped control character in string");
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++pos_;
                continue;
            }
            ++pos_;
            if (pos_ >= src_.size()) return syntaxError("Unterminated escape");
            char e = src_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!readHex4(cp)) return syntaxError("Invalid unicode escape");
                    if (unicode::isHighSurrogate(cp)) {
                        size_t save = pos_;
                        uint32_t low = 0;
                        if (pos_ + 1 < src_.size() && src_[pos_] == '\\' &&
                            src_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            if (!readHex4(low)) {
                                return syntaxError("Invalid unicode escape");
                            }
                            if (unicode::isLowSurrogate(low)) {
                                unicode::encodeCodePoint(
                                    unicode::combineSurrogates(cp, low), out);
                            } else {
                                // 不是低代理：回退，按孤立高代理输出。
                                pos_ = save;
                                unicode::encodeCodePoint(cp, out);
                            }
                        } else {
                            unicode::encodeCodePoint(cp, out);
                        }
                    } else {
                        unicode::encodeCodePoint(cp, out);
                    }
                    break;
                }
                default:
                    return syntaxError("Invalid escape character");
            }
        }
        return ctx_->createString(std::move(out));
    }

    Value* parseNumber() {
        size_t start = pos_;
        if (pos_ < src_.size() && src_[pos_] == '-') ++pos_;
        if (pos_ >= src_.size()) return syntaxError("Invalid number");

        if (src_[pos_] == '0') {
            ++pos_;
            if (pos_ < src_.size() && isDigit(src_[pos_])) {
                return syntaxError("Invalid number");
            }
        } else if (src_[pos_] >= '1' && src_[pos_] <= '9') {
            while (pos_ < src_.size() && isDigit(src_[pos_])) ++pos_;
        } else {
            return syntaxError("Invalid number");
        }

        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            if (pos_ >= src_.size() || !isDigit(src_[pos_])) {
                return syntaxError("Invalid number");
            }
            while (pos_ < src_.size() && isDigit(src_[pos_])) ++pos_;
        }

        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            if (pos_ >= src_.size() || !isDigit(src_[pos_])) {
                return syntaxError("Invalid number");
            }
            while (pos_ < src_.size() && isDigit(src_[pos_])) ++pos_;
        }

        std::string_view token = src_.substr(start, pos_ - start);
        double value = 0.0;
        auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec == std::errc::result_out_of_range) {
            // 溢出 → ±Infinity；下溢 → ±0（按指数符号近似判断）。
            bool negative = token[0] == '-';
            bool underflow = false;
            size_t epos = token.find_first_of("eE");
            if (epos != std::string_view::npos) {
                std::string_view exp = token.substr(epos + 1);
                bool expNegative = !exp.empty() && exp[0] == '-';
                int magnitude = 0;
                for (char ch : exp) {
                    if (ch >= '0' && ch <= '9') {
                        magnitude = magnitude * 10 + (ch - '0');
                        if (magnitude > 100000) break;
                    }
                }
                if (expNegative && magnitude > 308) underflow = true;
            }
            if (underflow) {
                value = negative ? -0.0 : 0.0;
            } else {
                value = negative ? -std::numeric_limits<double>::infinity()
                                 : std::numeric_limits<double>::infinity();
            }
        } else if (result.ec != std::errc{}) {
            return syntaxError("Invalid number");
        }
        return ctx_->createNumber(value);
    }
};

// —— reviver（InternalizeJSONProperty，ES2024 §25.5.1.1）——

Value* internalizeJsonProperty(IContext* ctx, Value* holder, Value* key, Value* reviver) {
    Value* value = ctx->getProperty(holder, key);
    if (detail::isErrorValue(value)) return value;

    if (detail::isObjectKind(value->type()->kind())) {
        if (isArrayValue(value)) {
            uint32_t length = 0;
            if (Value* err = detail::lengthOf(ctx, value, length)) return err;
            for (uint32_t i = 0; i < length; ++i) {
                Value* indexKey = ctx->createString(std::to_string(i));
                if (indexKey == ctx->oomValue()) return indexKey;
                Value* newElement =
                    internalizeJsonProperty(ctx, value, indexKey, reviver);
                if (detail::isErrorValue(newElement)) return newElement;
                if (newElement->type()->kind() == TypeKind::Undefined) {
                    Value* d = ctx->deleteProperty(value, indexKey);
                    if (detail::isErrorValue(d)) return d;
                } else {
                    Value* r = value->type()->defineProperty(
                        ctx, value, indexKey, newElement, true, true, true);
                    if (detail::isErrorValue(r)) return r;
                }
            }
        } else {
            std::vector<Value*> keys;
            if (Value* err = ownPropertyKeys(ctx, value, true, false, keys)) return err;
            for (Value* childKey : keys) {
                Value* newElement =
                    internalizeJsonProperty(ctx, value, childKey, reviver);
                if (detail::isErrorValue(newElement)) return newElement;
                if (newElement->type()->kind() == TypeKind::Undefined) {
                    Value* d = ctx->deleteProperty(value, childKey);
                    if (detail::isErrorValue(d)) return d;
                } else {
                    Value* r = value->type()->defineProperty(
                        ctx, value, childKey, newElement, true, true, true);
                    if (detail::isErrorValue(r)) return r;
                }
            }
        }
    }

    Value* args[2] = {key, value};
    return ctx->call(reviver, holder, 2, args);
}

// —— 序列化器 ——

void appendUnicodeEscape(std::string& out, uint32_t cp) {
    static const char kHex[] = "0123456789abcdef";
    out += "\\u";
    out.push_back(kHex[(cp >> 12) & 0xFu]);
    out.push_back(kHex[(cp >> 8) & 0xFu]);
    out.push_back(kHex[(cp >> 4) & 0xFu]);
    out.push_back(kHex[cp & 0xFu]);
}

// QuoteJSONString（ES2024 §25.5.2.2）：well-formed，孤立代理输出 \uXXXX。
void appendQuoted(std::string& out, std::string_view s) {
    out.push_back('"');
    size_t i = 0;
    while (i < s.size()) {
        size_t start = i;
        uint32_t cp = 0;
        if (!unicode::decodeCodePoint(s, i, cp)) {
            // 非法 UTF-8 字节：按 U+FFFD 输出，保证结果仍是合法 JSON。
            appendUnicodeEscape(out, 0xFFFDu);
            i = start + 1;
            continue;
        }
        switch (cp) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (cp < 0x20) {
                    appendUnicodeEscape(out, cp);
                } else if (unicode::isHighSurrogate(cp)) {
                    size_t next = i;
                    uint32_t low = 0;
                    if (next < s.size() &&
                        unicode::decodeCodePoint(s, next, low) &&
                        unicode::isLowSurrogate(low)) {
                        i = next;
                        unicode::encodeCodePoint(
                            unicode::combineSurrogates(cp, low), out);
                    } else {
                        appendUnicodeEscape(out, cp);
                    }
                } else if (unicode::isLowSurrogate(cp)) {
                    appendUnicodeEscape(out, cp);
                } else {
                    unicode::encodeCodePoint(cp, out);
                }
                break;
        }
    }
    out.push_back('"');
}

class JsonSerializer {
public:
    JsonSerializer(IContext* ctx, Value* replacer, Value* space)
        : ctx_(ctx), replacer_(replacer), space_(space) {}

    Value* run(Value* value) {
        if (Value* err = initReplacer()) return err;
        if (Value* err = initSpace()) return err;

        Value* wrapper = ctx_->createObject();
        if (wrapper == ctx_->oomValue()) return wrapper;
        Value* r = ctx_->defineProperty(wrapper, "", value, true, true, true);
        if (detail::isErrorValue(r)) return r;

        Value* key = ctx_->createString("");
        if (key == ctx_->oomValue()) return key;
        bool emitted = false;
        if (Value* err = serializeProperty(wrapper, key, emitted)) return err;
        if (!emitted) return ctx_->createUndefined();
        return ctx_->createString(std::move(out_));
    }

private:
    IContext* ctx_;
    Value* replacer_ = nullptr;
    Value* space_ = nullptr;

    Value* replacerFn_ = nullptr;
    bool hasPropertyList_ = false;
    std::vector<std::string> propertyList_;
    std::string gap_;
    std::string indent_;
    std::vector<Value*> stack_;
    std::string out_;

    Value* initReplacer() {
        if (replacer_ == nullptr) return nullptr;
        if (detail::isCallable(replacer_)) {
            replacerFn_ = replacer_;
            return nullptr;
        }
        if (!detail::isObjectKind(replacer_->type()->kind())) return nullptr;
        if (!isArrayValue(replacer_)) return nullptr;

        uint32_t length = 0;
        if (Value* err = detail::lengthOf(ctx_, replacer_, length)) return err;
        for (uint32_t i = 0; i < length; ++i) {
            Value* key = ctx_->createString(std::to_string(i));
            if (key == ctx_->oomValue()) return key;
            Value* v = ctx_->getProperty(replacer_, key);
            if (detail::isErrorValue(v)) return v;
            TypeKind k = v->type()->kind();
            if (k == TypeKind::PrimitiveWrapper) {
                Value* prim = wrapperPrimitiveValue(v);
                if (prim != nullptr) {
                    v = prim;
                    k = prim->type()->kind();
                }
            }
            if (k != TypeKind::String && k != TypeKind::Number) continue;
            std::string name = detail::valueToString(v);
            bool found = false;
            for (const std::string& existing : propertyList_) {
                if (existing == name) {
                    found = true;
                    break;
                }
            }
            if (!found) propertyList_.push_back(std::move(name));
        }
        hasPropertyList_ = true;
        return nullptr;
    }

    Value* initSpace() {
        if (space_ == nullptr) return nullptr;
        Value* v = space_;
        TypeKind k = v->type()->kind();
        if (k == TypeKind::PrimitiveWrapper) {
            Value* prim = wrapperPrimitiveValue(v);
            if (prim != nullptr) {
                v = prim;
                k = prim->type()->kind();
            }
        }
        if (k == TypeKind::Number) {
            double n = static_cast<NumberData*>(v->data())->value;
            if (std::isnan(n) || n <= 0.0) return nullptr;
            if (n > 10.0) n = 10.0;
            gap_.assign(static_cast<size_t>(n), ' ');
        } else if (k == TypeKind::String) {
            const std::string& s = static_cast<StringData*>(v->data())->value;
            size_t i = 0;
            int count = 0;
            while (i < s.size() && count < 10) {
                size_t start = i;
                uint32_t cp = 0;
                if (!unicode::decodeCodePoint(s, i, cp)) i = start + 1;
                gap_.append(s, start, i - start);
                ++count;
            }
        }
        return nullptr;
    }

    Value* serializeProperty(Value* holder, Value* key, bool& emitted) {
        emitted = false;
        Value* value = ctx_->getProperty(holder, key);
        if (detail::isErrorValue(value)) return value;

        if (detail::isObjectKind(value->type()->kind())) {
            Value* toJSON = ctx_->getProperty(value, "toJSON");
            if (detail::isErrorValue(toJSON)) return toJSON;
            if (detail::isCallable(toJSON)) {
                Value* args[1] = {key};
                value = ctx_->call(toJSON, value, 1, args);
                if (detail::isErrorValue(value)) return value;
            }
        }

        if (replacerFn_ != nullptr) {
            Value* args[2] = {key, value};
            value = ctx_->call(replacerFn_, holder, 2, args);
            if (detail::isErrorValue(value)) return value;
        }

        if (value->type()->kind() == TypeKind::PrimitiveWrapper) {
            Value* prim = wrapperPrimitiveValue(value);
            if (prim != nullptr) value = prim;
        }

        TypeKind k = value->type()->kind();
        switch (k) {
            case TypeKind::Null:
                out_ += "null";
                emitted = true;
                return nullptr;
            case TypeKind::Boolean:
                out_ += detail::toBoolean(value) ? "true" : "false";
                emitted = true;
                return nullptr;
            case TypeKind::String:
                appendQuoted(out_, static_cast<StringData*>(value->data())->value);
                emitted = true;
                return nullptr;
            case TypeKind::Number: {
                double n = static_cast<NumberData*>(value->data())->value;
                if (!std::isfinite(n)) {
                    out_ += "null";
                } else {
                    out_ += detail::numberToString(n);
                }
                emitted = true;
                return nullptr;
            }
            case TypeKind::Undefined:
            case TypeKind::Function:
            case TypeKind::Symbol:
                return nullptr;
            case TypeKind::BigInt:
                return detail::typeError(ctx_,
                                         "Do not know how to serialize a BigInt");
            default:
                break;
        }

        if (!detail::isObjectKind(k)) {
            return detail::typeError(ctx_, "Do not know how to serialize a BigInt");
        }

        for (Value* active : stack_) {
            if (active == value) {
                return detail::typeError(ctx_,
                                         "Converting circular structure to JSON");
            }
        }
        stack_.push_back(value);
        Value* err = isArrayValue(value) ? serializeArray(value)
                                         : serializeObject(value);
        stack_.pop_back();
        if (err != nullptr && detail::isErrorValue(err)) return err;
        emitted = true;
        return nullptr;
    }

    Value* serializeObject(Value* value) {
        std::vector<std::string> keys;
        if (hasPropertyList_) {
            keys = propertyList_;
        } else {
            std::vector<Value*> ownKeys;
            if (Value* err = ownPropertyKeys(ctx_, value, true, false, ownKeys)) {
                return err;
            }
            keys.reserve(ownKeys.size());
            for (Value* k : ownKeys) {
                if (k->type()->kind() == TypeKind::String) {
                    keys.push_back(static_cast<StringData*>(k->data())->value);
                }
            }
        }

        std::string savedIndent = indent_;
        indent_ += gap_;

        std::vector<std::string> parts;
        parts.reserve(keys.size());
        for (const std::string& name : keys) {
            Value* key = ctx_->createString(name);
            if (key == ctx_->oomValue()) return key;
            std::string saved = std::move(out_);
            out_.clear();
            bool emitted = false;
            Value* err = serializeProperty(value, key, emitted);
            std::string part = std::move(out_);
            out_ = std::move(saved);
            if (err != nullptr && detail::isErrorValue(err)) return err;
            if (!emitted) continue;

            std::string entry;
            appendQuoted(entry, name);
            entry += gap_.empty() ? ":" : ": ";
            entry += part;
            parts.push_back(std::move(entry));
        }
        appendContainer(parts, '{', '}', savedIndent);
        indent_ = std::move(savedIndent);
        return nullptr;
    }

    Value* serializeArray(Value* value) {
        uint32_t length = 0;
        if (Value* err = detail::lengthOf(ctx_, value, length)) return err;

        std::string savedIndent = indent_;
        indent_ += gap_;

        std::vector<std::string> parts;
        parts.reserve(length);
        for (uint32_t i = 0; i < length; ++i) {
            Value* key = ctx_->createString(std::to_string(i));
            if (key == ctx_->oomValue()) return key;
            std::string saved = std::move(out_);
            out_.clear();
            bool emitted = false;
            Value* err = serializeProperty(value, key, emitted);
            std::string part = std::move(out_);
            out_ = std::move(saved);
            if (err != nullptr && detail::isErrorValue(err)) return err;
            parts.push_back(emitted ? std::move(part) : std::string("null"));
        }
        appendContainer(parts, '[', ']', savedIndent);
        indent_ = std::move(savedIndent);
        return nullptr;
    }

    void appendContainer(const std::vector<std::string>& parts, char open, char close,
                         const std::string& savedIndent) {
        if (parts.empty()) {
            out_.push_back(open);
            out_.push_back(close);
            return;
        }
        if (gap_.empty()) {
            out_.push_back(open);
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) out_.push_back(',');
                out_ += parts[i];
            }
            out_.push_back(close);
            return;
        }

        out_.push_back(open);
        out_.push_back('\n');
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out_.push_back(',');
                out_.push_back('\n');
            }
            out_ += indent_;
            out_ += parts[i];
        }
        out_.push_back('\n');
        out_ += savedIndent;
        out_.push_back(close);
    }
};

// —— 内建入口 ——

Value* jsonParse(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* text = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* source = detail::toStringValue(ctx, text);
    if (detail::isErrorValue(source)) return source;

    const std::string& src = static_cast<StringData*>(source->data())->value;
    JsonParser parser(ctx, src);
    Value* result = parser.parse();
    if (detail::isErrorValue(result)) return result;

    if (argc > 1 && detail::isCallable(argv[1])) {
        Value* holder = ctx->createObject();
        if (holder == ctx->oomValue()) return holder;
        Value* r = ctx->defineProperty(holder, "", result, true, true, true);
        if (detail::isErrorValue(r)) return r;
        Value* key = ctx->createString("");
        if (key == ctx->oomValue()) return key;
        return internalizeJsonProperty(ctx, holder, key, argv[1]);
    }
    return result;
}

Value* jsonStringify(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* replacer = argc > 1 ? argv[1] : nullptr;
    Value* space = argc > 2 ? argv[2] : nullptr;
    JsonSerializer serializer(ctx, replacer, space);
    return serializer.run(value);
}

// minArgc 同时被 createNativeFunction 用作 length 属性，且会做最少参数校验；
// JSON.parse/stringify 允许 0 参调用（由算法自己处理），故用 0 创建后单独设 length。
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t length) {
    Value* f = ctx->createNativeFunction(fn, 0);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    Value* lengthValue = ctx->createNumber(static_cast<double>(length));
    if (lengthValue == ctx->oomValue()) return false;
    if (ctx->defineProperty(f, "length", lengthValue, false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerJSONBuiltins(IContext* ctx) {
    Value* json = ctx->createObject();
    if (json == ctx->oomValue()) return false;

    if (!defineMethod(ctx, json, "parse", jsonParse, 2)) return false;
    if (!defineMethod(ctx, json, "stringify", jsonStringify, 3)) return false;

    Value* tag = ctx->createString("JSON");
    if (tag == ctx->oomValue()) return false;
    if (ctx->defineProperty(json, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            tag, false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    ctx->global()->add(json);
    ctx->global()->bind("JSON", json);
    return true;
}

}  // namespace neo::core
