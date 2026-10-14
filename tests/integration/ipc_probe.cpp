/**
 * @file ipc_probe.cpp
 * @brief ipc_probe 实现（测试与集成探针）。
 * @details 文件位置：tests/integration/ipc_probe.cpp（集成探针/测试替身）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

// 集成探针：供 scripts/verify-p2.sh 调用真实 lswd。
// 用法: lsw_ipc_probe <socket-path> <command> [json-args]
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <lsw/ipc/client.hpp>

/// @brief IPC 集成探针入口，向 lswd 发送命令并打印响应。
/// @param argc 命令行参数数量。
/// @param argv 命令行参数数组：<socket-path> <command> [json-args]。
/// @return 0 表示 IPC 调用成功，非 0 表示参数或调用失败。
int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr
            << "用法: lsw_ipc_probe <socket-path> <command> [json-args]\n";
        return 2;
    }

    nlohmann::json args = nlohmann::json::object();
    if (argc >= 4) {
        args = nlohmann::json::parse(argv[3], nullptr, false);
        if (args.is_discarded()) {
            std::cerr << "ERROR: 非法 JSON 参数: " << argv[3] << "\n";
            return 2;
        }
    }

    lsw::ipc::ClientOptions options;
    options.socket_path = argv[1];
    options.io_timeout = std::chrono::milliseconds(3000);
    lsw::ipc::Client client(options);

    const auto result = client.call(argv[2], args);
    if (!result) {
        std::cerr << "ERROR " << lsw::to_string(result.error().code) << ": "
                  << result.error().message << "\n";
        return 1;
    }
    std::cout << result->dump() << "\n";
    return 0;
}
