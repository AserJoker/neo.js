// sema/SemaOptions.h - Sema 输入选项
//
// 脚本/模块使用默认值；eval 由运行时提供调用方上下文快照，使早期错误
// 判定继承调用方的严格模式与控制流上下文（§19.2.1.1）。
//
// 关联 ADR: 033（eval）、057（严格模式）、058（Sema）

#ifndef NEO_SEMA_SEMA_OPTIONS_H
#define NEO_SEMA_SEMA_OPTIONS_H

namespace neo::sema {

struct SemaOptions {
    // 直接 eval 继承调用方严格模式；脚本/模块默认 false。
    bool inheritedStrict = false;
    // 源码按 eval 编译（SourceType::Eval），Program 上下文取下列字段。
    bool evalContext = false;
    // eval 调用方处于函数/方法/派生构造器上下文时，允许对应语法。
    bool allowsNewTarget = false;
    bool allowsSuperCall = false;
    bool allowsSuperProperty = false;
    // class 字段初始化器中的 eval 禁止 arguments（§19.2.1.1 11.h）。
    bool allowsArguments = true;
};

}  // namespace neo::sema

#endif  // NEO_SEMA_SEMA_OPTIONS_H
