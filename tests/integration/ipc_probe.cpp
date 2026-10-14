// 集成探针：供 scripts/verify-p2.sh 调用真实 lswd。
// 用法: lsw_ipc_probe <socket-path> <command> [json-args]
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <lsw/ipc/client.hpp>

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
