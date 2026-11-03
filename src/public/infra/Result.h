// Result.h - Result<T> 统一错误返回类型
//
// neo.js 不抛 C++ 异常（ADR-007），FFI 边界 catch 标准库异常转 Result::err
// （ADR-006 异常即值）。Result<T> 是值或错误码的二元 union，无堆分配。
//
// 用法：
//   Result<int> r = Result<int>::ok(42);
//   if (r.is_ok()) { use(r.value()); }
//   else { handle(r.err()); }
//
// 关联 ADR: 006（异常即值）、007（无异常）、028（infra）

#ifndef NEO_INFRA_RESULT_H
#define NEO_INFRA_RESULT_H

#include <utility>

namespace neo::infra {

// 错误码（按需扩展）
enum class ErrCode : int {
    Ok = 0,
    InvalidArg,
    OutOfRange,
    IoError,
    NotFound,
    AlreadyExists,
    OutOfMemory,
    Unsupported,
    ParseError,
};

// Result<T> — 值或错误码
template <typename T>
class Result {
    bool ok_;
    union {
        T      value_;
        ErrCode err_;
    };

    // 私有构造 tag
    struct OkTag {};
    struct ErrTag {};
    Result(OkTag, T v) : ok_(true)  { new (&value_) T(std::move(v)); }
    Result(ErrTag, ErrCode e) : ok_(false) { err_ = e; }

public:
    // 工厂：成功
    static Result ok(T v)            { return Result(OkTag{},  std::move(v)); }
    // 工厂：失败
    static Result err(ErrCode e)     { return Result(ErrTag{}, e); }

    Result(const Result& o) : ok_(o.ok_) {
        if (ok_) new (&value_) T(o.value_);
        else     err_ = o.err_;
    }
    Result(Result&& o) noexcept : ok_(o.ok_) {
        if (ok_) new (&value_) T(std::move(o.value_));
        else     err_ = o.err_;
    }
    Result& operator=(const Result& o) {
        if (this != &o) {
            destroy();
            ok_ = o.ok_;
            if (ok_) new (&value_) T(o.value_);
            else     err_ = o.err_;
        }
        return *this;
    }
    Result& operator=(Result&& o) noexcept {
        if (this != &o) {
            destroy();
            ok_ = o.ok_;
            if (ok_) new (&value_) T(std::move(o.value_));
            else     err_ = o.err_;
        }
        return *this;
    }
    ~Result() { destroy(); }

    bool is_ok()  const { return ok_; }
    bool is_err() const { return !ok_; }

    T&       value()       { return value_; }  // is_err() 时 UB（编程错误）
    const T& value() const { return value_; }
    ErrCode  err()   const { return err_; }

private:
    void destroy() {
        if (ok_) value_.~T();
    }
};

// Result<void> 特化 — 仅 ok/err，无值
template <>
class Result<void> {
    bool    ok_;
    ErrCode err_;

    struct OkTag {};
    struct ErrTag {};
    Result(OkTag)         : ok_(true),  err_(ErrCode::Ok) {}
    Result(ErrTag, ErrCode e) : ok_(false), err_(e) {}

public:
    static Result ok()              { return Result(OkTag{}); }
    static Result err(ErrCode e)    { return Result(ErrTag{}, e); }

    bool    is_ok()  const { return ok_; }
    bool    is_err() const { return !ok_; }
    ErrCode err()    const { return err_; }
};

}  // namespace neo::infra

#endif  // NEO_INFRA_RESULT_H
