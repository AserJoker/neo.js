// TestData.h - 测试用扁平 Data 子类（仅测试，不入库生产代码）
//
// 本阶段不实现正式 Type 体系（NumberType 等），测试用最小 Data 子类验证
// Value/Data 引用计数与 GC 生命周期。提供 NumberData（持 double）和
// TagData（持 int 标签，用于区分实例）。
//
// 关联 ADR: 015（Value meta+data）、029（扁平 Value 阶段）

#ifndef NEO_TEST_CORE_TEST_DATA_H
#define NEO_TEST_CORE_TEST_DATA_H

#include "core/IData.h"
#include "core/Value.h"  // Type 前向声明

namespace neo::core::test {

// 持 double 的扁平 data
class NumberData : public IData {
public:
    double value;
    explicit NumberData(double v) : value(v) {}
};

// 持 int 标签的扁平 data（测试中区分不同实例）
class TagData : public IData {
public:
    int tag;
    explicit TagData(int t) : tag(t) {}
};

// 测试用 Type 占位（本阶段 Type 体系未实现，用空类占位）
// 返回 neo::core::IType* 所需的占位实例（reinterpret_cast，仅测试用）
class DummyType {};
inline IType* dummy_type() {
    static DummyType inst{};
    return reinterpret_cast<IType*>(&inst);
}

}  // namespace neo::core::test

#endif  // NEO_TEST_CORE_TEST_DATA_H
