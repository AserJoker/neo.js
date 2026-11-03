# tests/integration/CMakeLists.txt - 集成测试
#
# 多模块协作端到端测试，真实依赖不 mock。
# M2 起填充：core/（Context 端到端）、pipeline/（五阶段端到端）等。

add_subdirectory(core)
# add_subdirectory(pipeline)  # 五阶段端到端待 VM 就绪后启用
