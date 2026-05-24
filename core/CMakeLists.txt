cmake_minimum_required(VERSION 3.20)
project(chargerlog_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: 强制 UTF-8 编码，避免中文注释/字符串编译错误 (C4819, C2001)
if(MSVC)
    add_compile_options("/utf-8")
endif()

# MinGW: 静态链接运行时库，避免 DLL 依赖
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# ── 库 ──────────────────────────────────────────────
add_library(chargerlog_core
    src/charger_data_point.cpp
    src/healthd_parser.cpp
    src/stats_calculator.cpp
    src/project_config.cpp
    src/cache_manager.cpp
    src/parser_factory.cpp
)

target_include_directories(chargerlog_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── CLI 可执行文件 ──────────────────────────────────────
add_executable(chargerlog
    cli_main.cpp
)
target_link_libraries(chargerlog PRIVATE chargerlog_core)
target_include_directories(chargerlog
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── 测试可执行文件 ──────────────────────────────────────
add_executable(test_healthd_parser
    ${PROJECT_SOURCE_DIR}/../tests/test_healthd_parser.cpp
)

target_link_libraries(test_healthd_parser PRIVATE chargerlog_core)
target_include_directories(test_healthd_parser
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(test_cache_manager
    ${PROJECT_SOURCE_DIR}/../tests/test_cache_manager.cpp
)
target_link_libraries(test_cache_manager PRIVATE chargerlog_core)
target_include_directories(test_cache_manager
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
