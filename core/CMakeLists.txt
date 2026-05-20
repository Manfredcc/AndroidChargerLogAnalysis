cmake_minimum_required(VERSION 3.20)
project(chargerlog_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 库 ──────────────────────────────────────────────
add_library(chargerlog_core
    src/charger_data_point.cpp
    src/healthd_parser.cpp
    src/stats_calculator.cpp
    src/project_config.cpp
)

target_include_directories(chargerlog_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── CLI 可执行文件 ──────────────────────────────────────
add_executable(chargerlog
    ${PROJECT_SOURCE_DIR}/../cli/main.cpp
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
