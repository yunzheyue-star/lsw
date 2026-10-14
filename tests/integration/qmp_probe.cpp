/**
 * @file qmp_probe.cpp
 * @brief qmp_probe 实现（测试与集成探针）。
 * @details 文件位置：tests/integration/qmp_probe.cpp（集成探针/测试替身）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 */

// QMP 探针：供 scripts/verify-p3.sh 对真实 QEMU 做冒烟测试。
// 用法: lsw_qmp_probe <qmp-socket> [command] [json-args]
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <lsw/vm/qmp_client.hpp>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "用法: lsw_qmp_probe <qmp-socket> [command] [json-args]\n";
        return 2;
    }
    const std::string command = argc >= 3 ? argv[2] : "query-status";
    nlohmann::json args = nlohmann::json::object();
    if (argc >= 4) {
        args = nlohmann::json::parse(argv[3], nullptr, false);
        if (args.is_discarded()) {
            std::cerr << "ERROR: 非法 JSON 参数\n";
            return 2;
        }
    }

    lsw::vm::QmpOptions options;
    options.socket_path = argv[1];
    options.connect_timeout = std::chrono::milliseconds(3000);
    options.io_timeout = std::chrono::milliseconds(3000);
    lsw::vm::QmpClient client(options);

    if (auto connected = client.connect(); !connected) {
        std::cerr << "ERROR " << lsw::to_string(connected.error().code) << ": "
                  << connected.error().message << "\n";
        return 1;
    }
    const auto result = client.execute(command, args);
    if (!result) {
        std::cerr << "ERROR " << lsw::to_string(result.error().code) << ": "
                  << result.error().message << "\n";
        return 1;
    }
    std::cout << result->dump() << "\n";
    return 0;
}
