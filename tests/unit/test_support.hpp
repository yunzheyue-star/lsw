#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace lsw::test {

/// 测试用临时目录：构造时创建，析构时递归删除。
class TempDir {
 public:
  explicit TempDir(const std::string& tag) {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("lsw-test-" + tag + "-" + std::to_string(::getpid()) + "-" +
             std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

/// 在常见安装位置中解析二进制；找不到时原样返回名称。
[[nodiscard]] inline std::string binary(const std::string& name) {
  for (const char* base : {"/usr/bin/", "/bin/", "/usr/local/bin/"}) {
    const std::filesystem::path candidate = std::string(base) + name;
    if (std::filesystem::exists(candidate)) {
      return candidate.string();
    }
  }
  return name;
}

}  // namespace lsw::test
