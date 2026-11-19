cmake_minimum_required(VERSION 3.20)

project(neojs
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "neo.js - embeddable ECMAScript standard runtime"
)

# ---------------------------------------------------------------------------
# 全局策略
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)        # 允许 clang 扩展（双平台同步用 clang）
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 默认 Release（单配置生成器如 Ninja），多配置生成器由 preset 指定
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# 导出 compile_commands.json（编辑器/工具集成）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# 选项
# ---------------------------------------------------------------------------
option(NEO_ENABLE_ICU "Enable ICU Unicode subsystem (default OFF, M1+)" OFF)
option(NEO_ENABLE_TESTS "Build tests (default ON)" ON)
option(NEO_BUILD_HELLO "Build HelloWorld executable (default ON)" ON)
option(NEO_ENABLE_COVERAGE "Enable llvm-cov source-based coverage (ADR-027)" OFF)
option(NEO_BUILD_FUZZERS "Build libFuzzer targets (Clang only, ADR-062)" OFF)

# clang_rt.fuzzer 在 Windows 上使用 Release 静态 CRT；必须与项目运行时一致。
if(NEO_BUILD_FUZZERS)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR
            "NEO_BUILD_FUZZERS requires CMAKE_BUILD_TYPE=Release "
            "(clang_rt.fuzzer uses the release static CRT)")
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# 编译器配置：强制 Clang
# ---------------------------------------------------------------------------
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "neo.js requires Clang (found: ${CMAKE_CXX_COMPILER_ID}). "
        "MSVC is not supported. Use CMakePresets.json to select clang.")
endif()

# 告警即错误（ADR-007）
# 注意：只作用于本项目 target，不影响 third_party（GTest/ICU 等第三方代码
# 不应被我们的 -Werror 约束）
set(NEO_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wconversion
    -Wnull-dereference
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wformat=2
)

# 调试符号（即使 Release 也保留，便于 ASan/调试）
set(NEO_DEBUG_FLAG "")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(NEO_DEBUG_FLAG -g)
endif()

# ---------------------------------------------------------------------------
# 项目内编译警告接口（仅本项目 target 链接，third_party 不受影响）
# ---------------------------------------------------------------------------
add_library(neo_warnings INTERFACE)
target_compile_options(neo_warnings INTERFACE ${NEO_WARNING_FLAGS} ${NEO_DEBUG_FLAG})

# ---------------------------------------------------------------------------
# 覆盖率采集（llvm-cov source-based，ADR-027）
# ---------------------------------------------------------------------------
include(cmake/coverage.cmake)
if(NEO_ENABLE_COVERAGE)
    message(STATUS "neo.js: coverage instrumentation ENABLED (llvm-cov source-based)")
endif()

# ---------------------------------------------------------------------------
# ICU 子系统（默认关闭，M1+ 启用）
# ---------------------------------------------------------------------------
if(NEO_ENABLE_ICU)
    set(ICU_LOCAL_SRC_ZIP  "" CACHE FILEPATH "Local ICU source zip")
    set(ICU_LOCAL_DATA_ZIP "" CACHE FILEPATH "Local ICU data zip")
    set(ICU_LOCAL_DATA_DAT "" CACHE FILEPATH "Local prebuilt icudt*.dat")
    add_subdirectory(third_party/icu)
    message(STATUS "neo.js: ICU subsystem ENABLED")
else()
    message(STATUS "neo.js: ICU subsystem disabled (set -DNEO_ENABLE_ICU=ON to enable)")
endif()

# ---------------------------------------------------------------------------
# neojs 库（M0 占位，M1 起充实）
# ---------------------------------------------------------------------------
add_library(neojs STATIC
    src/private/core/HelloWorld.cpp
    src/private/core/Value.cpp
    src/private/core/MarkSweepGC.cpp
    src/private/core/Scope.cpp
    src/private/core/Context.cpp
    src/private/core/Task.cpp
    src/private/core/ObjectBuiltins.cpp
    src/private/core/ProxyBuiltins.cpp
    src/private/core/ReflectBuiltins.cpp
    src/private/core/ArrayBuiltins.cpp
    src/private/core/IteratorBuiltins.cpp
    src/private/core/GeneratorBuiltins.cpp
    src/private/core/AsyncGeneratorBuiltins.cpp
    src/private/core/Module.cpp
    src/private/core/OperatorPrimitives.cpp
    src/private/core/TimerBuiltins.cpp
    src/private/core/EvalBuiltins.cpp
    src/private/core/ErrorBuiltins.cpp
    src/private/core/PromiseBuiltins.cpp
    src/private/core/ErrorKind.cpp
    src/private/core/PrimitiveWrapperBuiltins.cpp
    src/private/core/RegExpBuiltins.cpp
    src/private/core/BigIntBuiltins.cpp
    src/private/core/SymbolBuiltins.cpp
    src/private/core/Types.cpp
    src/private/core/types/Undefined.cpp
    src/private/core/types/Null.cpp
    src/private/core/types/Boolean.cpp
    src/private/core/types/Number.cpp
    src/private/core/types/BigInt.cpp
    src/private/core/types/String.cpp
    src/private/core/types/Exception.cpp
    src/private/core/types/Fatal.cpp
    src/private/core/types/Uninitialized.cpp
    src/private/core/types/Function.cpp
    src/private/core/types/Object.cpp
    src/private/core/types/Array.cpp
    src/private/core/types/Arguments.cpp
    src/private/core/types/Iterator.cpp
    src/private/core/types/Collection.cpp
    src/private/core/types/Date.cpp
    src/private/core/types/ArrayBuffer.cpp
    src/private/core/types/DataView.cpp
    src/private/core/types/TypedArray.cpp
    src/private/core/types/IntlNumberFormat.cpp
    src/private/core/types/IntlDateTimeFormat.cpp
    src/private/core/types/IntlCollator.cpp
    src/private/core/types/IntlPluralRules.cpp
    src/private/core/types/IntlRelativeTimeFormat.cpp
    src/private/core/types/Error.cpp
    src/private/core/types/PrimitiveWrapper.cpp
    src/private/core/types/Promise.cpp
    src/private/core/types/Symbol.cpp
    src/private/core/types/Interrupt.cpp
    src/private/core/types/Coroutine.cpp
    src/private/core/types/AsyncGenerator.cpp
    src/private/core/types/ModuleNamespace.cpp
    src/private/core/types/Proxy.cpp
    src/private/core/NativeCoroutine.cpp
    src/private/core/AsyncNative.cpp
    src/private/core/JSONBuiltins.cpp
    src/private/core/MathBuiltins.cpp
    src/private/core/GlobalFunctionBuiltins.cpp
    src/private/core/StringBuiltins.cpp
    src/private/core/NumberBuiltins.cpp
    src/private/core/CollectionBuiltins.cpp
    src/private/core/ConsoleBuiltins.cpp
    src/private/core/DateBuiltins.cpp
    src/private/core/ArrayBufferBuiltins.cpp
    src/private/core/TypedArrayBuiltins.cpp
    src/private/core/IntlBuiltins.cpp
    src/private/core/intl/MinimalIntlBackend.cpp
    src/private/core/FunctionInvoke.cpp
    src/private/unicode/Utf8.cpp
    src/private/unicode/UnicodeClassification.cpp
    src/private/regex/RegexFlags.cpp
    src/private/regex/RegexParser.cpp
    src/private/regex/RegexCompiler.cpp
    src/private/regex/RegexUnicode.cpp
    src/private/regex/RegexVM.cpp
    src/private/regex/RegexPikeVM.cpp
    src/private/regex/RegexSubject.cpp
    src/private/regex/RegexEngine.cpp
    src/private/bigint/BigInt.cpp
    src/private/infra/MallocAllocator.cpp
    src/private/infra/Console.cpp
    src/private/infra/SteadyClock.cpp
    src/private/infra/CommandParser.cpp
    src/private/infra/NativeFileSystem.cpp
    src/private/frontend/CharStream.cpp
    src/private/frontend/Lexer.cpp
    src/private/frontend/Node.cpp
    src/private/frontend/Parser.cpp
    src/private/sema/Walker.cpp
    src/private/sema/StrictnessPass.cpp
    src/private/sema/ScopePass.cpp
    src/private/sema/ResolvePass.cpp
    src/private/sema/ControlFlowPass.cpp
    src/private/sema/ModulePass.cpp
    src/private/sema/EarlyErrorPass.cpp
    src/private/sema/Sema.cpp
    src/private/compiler/Compiler.cpp
    src/private/vm/VM.cpp
)
target_include_directories(neojs
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/public      # public 头暴露给消费者（接口/类型声明）
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/private
)
target_link_libraries(neojs PUBLIC neo_warnings)

# 覆盖率插桩（启用时给库 PUBLIC 传播，让链接它的测试也带覆盖率）
if(NEO_ENABLE_COVERAGE)
    neo_enable_coverage_public(neojs)
endif()

# ICU 链接（仅启用时）
if(NEO_ENABLE_ICU)
    target_link_libraries(neojs PRIVATE ICU::uc ICU::i18n ICU::data)
    target_compile_definitions(neojs PUBLIC NEO_HAS_ICU=1)
endif()

# ---------------------------------------------------------------------------
# HelloWorld 可执行
# ---------------------------------------------------------------------------
if(NEO_BUILD_HELLO)
    add_executable(neo_hello src/private/core/HelloMain.cpp)
    target_link_libraries(neo_hello PRIVATE neojs)
    set_target_properties(neo_hello PROPERTIES OUTPUT_NAME neo_hello)
    # neo_hello 通过 neojs 间接获得 neo_warnings（PUBLIC 传播）
endif()

# ---------------------------------------------------------------------------
# 测试（分层：unit/integration/regression，ADR-027）
# ---------------------------------------------------------------------------
if(NEO_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(third_party/googletest)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# libFuzzer 目标（默认关闭；开启时给 neojs 插桩，fuzz 可执行提供 fuzzer 入口）
# ---------------------------------------------------------------------------
if(NEO_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "NEO_BUILD_FUZZERS requires Clang")
    endif()
    # Windows 上 clang_rt.fuzzer 与 ASan 运行时的 ABI 注解不一致，默认只启用
    # 覆盖率引导；需要 ASan/UBSan 时请用匹配的 sanitizer 运行时单独构建。
    target_compile_options(neojs PRIVATE -fsanitize=fuzzer-no-link)

    add_executable(regex_fuzz tests/fuzz/RegexFuzz.cpp)
    target_include_directories(regex_fuzz PRIVATE
        ${CMAKE_SOURCE_DIR}/src/private)
    target_link_libraries(regex_fuzz PRIVATE neojs)
    target_compile_options(regex_fuzz PRIVATE -fsanitize=fuzzer)
    target_link_options(regex_fuzz PRIVATE -fsanitize=fuzzer)
endif()
