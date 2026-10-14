# LSW 测试清单
#
# 阶段 0：先提供冒烟测试，确保 lsw 可执行文件能够运行。
# 后续在对应阶段追加单元测试（如 lsw_core 的路径映射、配置解析、IPC 编解码）。

add_test(NAME lsw_smoke COMMAND $<TARGET_FILE:lsw>)
set_tests_properties(lsw_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "Hello, from lsw!"
    LABELS "smoke"
)
