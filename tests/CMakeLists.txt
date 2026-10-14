# LSW 测试清单
#
# - lsw_smoke：可执行文件冒烟测试（P0 基线）
# - lsw_core_tests：lsw_core 单元测试（Catch2 v3，P1 起）

# ===== 冒烟测试 =====
add_test(NAME lsw_smoke COMMAND $<TARGET_FILE:lsw>)
set_tests_properties(lsw_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "Hello, from lsw!"
    LABELS "smoke"
)

# ===== lsw_core 单元测试 =====
lsw_resolve_catch2()

file(GLOB_RECURSE LSW_CORE_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
)
if(NOT LSW_CORE_TEST_SOURCES)
    message(FATAL_ERROR "tests/unit 下没有找到任何测试源文件")
endif()

add_executable(lsw_core_tests ${LSW_CORE_TEST_SOURCES})
target_link_libraries(lsw_core_tests PRIVATE lsw_core Catch2::Catch2WithMain)
target_include_directories(lsw_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")

include(Catch)
catch_discover_tests(lsw_core_tests
    LABELS "unit"
    TEST_PREFIX "core."
)

# ===== 集成探针：供 scripts/verify-p2.sh 调用真实 lswd =====
add_executable(lsw_ipc_probe "${CMAKE_CURRENT_SOURCE_DIR}/integration/ipc_probe.cpp")
target_link_libraries(lsw_ipc_probe PRIVATE lsw_core)
