# LSW 测试清单
#
# - lsw_smoke：可执行文件冒烟测试（P0 基线）
# - lsw_core_tests：lsw_core 单元测试（Catch2 v3，P1 起）

# ===== 冒烟测试 =====
add_test(NAME lsw_smoke COMMAND $<TARGET_FILE:lsw> --version)
set_tests_properties(lsw_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "lsw 0\\.1\\.0"
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

# ===== 集成探针与测试替身 =====
add_executable(lsw_ipc_probe "${CMAKE_CURRENT_SOURCE_DIR}/integration/ipc_probe.cpp")
target_link_libraries(lsw_ipc_probe PRIVATE lsw_core)

# 假 QEMU：无 KVM/固件环境下提供真实 QMP 行为
add_executable(lsw_fake_qemu "${CMAKE_CURRENT_SOURCE_DIR}/integration/fake_qemu.cpp")
target_link_libraries(lsw_fake_qemu PRIVATE lsw_core)

# 真实 QEMU QMP 冒烟探针
add_executable(lsw_qmp_probe "${CMAKE_CURRENT_SOURCE_DIR}/integration/qmp_probe.cpp")
target_link_libraries(lsw_qmp_probe PRIVATE lsw_core)

# 安装流程集成探针（假 QEMU）
add_executable(lsw_install_probe "${CMAKE_CURRENT_SOURCE_DIR}/integration/install_probe.cpp")
target_link_libraries(lsw_install_probe PRIVATE lsw_core)

# 单元测试需要知道假 QEMU 的构建路径
target_compile_definitions(lsw_core_tests PRIVATE
    LSW_FAKE_QEMU_PATH="$<TARGET_FILE:lsw_fake_qemu>")
add_dependencies(lsw_core_tests lsw_fake_qemu)
