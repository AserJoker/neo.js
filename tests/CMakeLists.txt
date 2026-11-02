# tests/CMakeLists.txt - 测试汇总
#
# 测试分层（ADR-027）：
#   unit/         单元测试（mock 依赖，隔离）
#   integration/  集成测试（真实依赖，端到端）
#   regression/   回归测试（历史 bug 用例）
#
# 每个测试可执行对应一个模块或集成场景。M0/M1 宽松期，M2 起强制覆盖率门禁。

# 单元测试
add_subdirectory(unit)

# 集成测试
add_subdirectory(integration)

# 回归测试（M2 起填充）
add_subdirectory(regression)
