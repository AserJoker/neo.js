# cmake/coverage.cmake - 覆盖率采集辅助（llvm-cov source-based）
#
# 用法（在主 CMakeLists.txt 中 include）：
#   include(cmake/coverage)
#   neo_enable_coverage(<target>)
#
# 启用后目标编译/链接加 -fprofile-instr-generate -fcoverage-mapping，
# 运行时生成 .profraw，由 covgen.py 脚本合并生成报告。
#
# 关联 ADR: 026（clang-only）、027（llvm-cov source-based）

# 覆盖率编译/链接标志
set(NEO_COVERAGE_FLAGS
    -fprofile-instr-generate
    -fcoverage-mapping
)

# 给指定目标启用覆盖率采集
function(neo_enable_coverage target)
    target_compile_options(${target} PRIVATE ${NEO_COVERAGE_FLAGS})
    target_link_options(${target} PRIVATE -fprofile-instr-generate)
endfunction()

# 给指定目标的 PUBLIC 传播（用于库，让链接它的测试也带覆盖率）
function(neo_enable_coverage_public target)
    target_compile_options(${target} PUBLIC ${NEO_COVERAGE_FLAGS})
    target_link_options(${target} PUBLIC -fprofile-instr-generate)
endfunction()
