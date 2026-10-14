// 假 QEMU 测试替身：只实现 QMP 握手/状态/电源命令，用于无 KVM/无固件环境。
// 用法: lsw_fake_qemu <QEMU 参数...>（必须包含 -qmp unix:<path>,server=on,wait=off）
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

bool write_line(int fd, const nlohmann::json& value) {
  const std::string line = value.dump() + "\n";
  std::size_t offset = 0;
  while (offset < line.size()) {
    const ssize_t count =
        ::send(fd, line.data() + offset, line.size() - offset, MSG_NOSIGNAL);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return true;
}

bool read_line(int fd, std::string& out) {
  out.clear();
  while (!g_stop) {
    pollfd pfd{fd, POLLIN, 0};
    const int ready = ::poll(&pfd, 1, 200);
    if (ready == 0) {
      continue;
    }
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    char chunk[4096];
    const ssize_t count = ::recv(fd, chunk, sizeof(chunk), 0);
    if (count > 0) {
      out.append(chunk, static_cast<std::size_t>(count));
      if (const auto pos = out.find('\n'); pos != std::string::npos) {
        out.resize(pos);
        return true;
      }
      continue;
    }
    if (count == 0) {
      return false;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    return false;
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  std::string qmp_spec;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "-qmp") {
      qmp_spec = argv[i + 1];
    }
  }
  if (qmp_spec.rfind("unix:", 0) != 0) {
    std::cerr << "fake-qemu: 缺少 -qmp unix:<path>,server=on,wait=off 参数\n";
    return 2;
  }
  std::string path = qmp_spec.substr(5);
  if (const auto comma = path.find(','); comma != std::string::npos) {
    path.resize(comma);
  }
  if (path.size() >= sizeof(sockaddr_un::sun_path)) {
    std::cerr << "fake-qemu: socket 路径过长\n";
    return 2;
  }

  std::signal(SIGTERM, on_signal);
  std::signal(SIGINT, on_signal);
  std::signal(SIGPIPE, SIG_IGN);

  ::unlink(path.c_str());
  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listen_fd < 0) {
    std::cerr << "fake-qemu: socket 创建失败\n";
    return 1;
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size());
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      ::chmod(path.c_str(), 0700) != 0 || ::listen(listen_fd, 4) != 0) {
    std::cerr << "fake-qemu: 监听失败: " << std::strerror(errno) << "\n";
    return 1;
  }

  int client = -1;
  while (!g_stop && client < 0) {
    pollfd pfd{listen_fd, POLLIN, 0};
    const int ready = ::poll(&pfd, 1, 200);
    if (ready > 0) {
      client = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
    }
  }
  if (client < 0) {
    ::unlink(path.c_str());
    return 0;
  }

  (void)write_line(client, nlohmann::json{
      {"QMP", {{"version", {{"qemu", {{"major", 8}, {"minor", 0}, {"micro", 0}}},
                            {"package", "lsw-fake"}}},
               {"capabilities", nlohmann::json::array()}}}});

  std::string line;
  while (!g_stop && read_line(client, line)) {
    const auto request = nlohmann::json::parse(line, nullptr, false);
    if (request.is_discarded() || !request.is_object() ||
        !request.contains("execute")) {
      continue;
    }
    const std::string command = request["execute"].get<std::string>();
    const bool has_id = request.contains("id");
    const auto id = has_id ? request["id"] : nlohmann::json();

    if (command == "no-reply") {
      continue;  // 故意不回包，用于客户端超时测试
    }

    // 模拟真实 QEMU：响应前先发一个异步事件，客户端必须跳过。
    (void)write_line(client, nlohmann::json{{"event", "RESET"}, {"data", {}}});

    nlohmann::json response;
    bool should_quit = false;
    if (command == "qmp_capabilities") {
      response = nlohmann::json{{"return", nlohmann::json::object()}};
    } else if (command == "query-status") {
      response = nlohmann::json{{"return", {{"status", "running"}}}};
    } else if (command == "system_powerdown") {
      response = nlohmann::json{{"return", nlohmann::json::object()}};
      should_quit = true;
    } else if (command == "quit") {
      response = nlohmann::json{{"return", nlohmann::json::object()}};
      should_quit = true;
    } else {
      response = nlohmann::json{
          {"error", {{"class", "CommandNotFound"}, {"desc", "fake-qemu"}}}};
    }
    if (has_id) {
      response["id"] = id;
    }
    if (!write_line(client, response)) {
      break;
    }
    if (should_quit) {
      break;
    }
  }

  ::close(client);
  ::close(listen_fd);
  ::unlink(path.c_str());
  return 0;
}
