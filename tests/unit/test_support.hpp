/**
 * @file test_support.hpp
 * @brief test_support 接口声明（测试与集成探针）。
 * @details 文件位置：tests/unit/test_support.hpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace lsw::test {

/// @brief 测试用临时目录：构造时创建，析构时递归删除。
class TempDir {
  public:
    /// @brief 构造测试用临时目录并创建对应文件系统目录。
    /// @param tag 用于构成临时目录名的标签。
    explicit TempDir(const std::string &tag) {
        static std::atomic<int> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("lsw-test-" + tag + "-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    /// @brief 析构时递归删除临时目录及其内容。
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    /// @brief 禁止拷贝构造临时目录对象。
    /// @param other 被删除的拷贝源（声明中未命名）。
    TempDir(const TempDir &) = delete;
    /// @brief 禁止拷贝赋值临时目录对象。
    /// @param other 被删除的赋值源（声明中未命名）。
    /// @return 临时目录引用（该运算符已被删除）。
    TempDir &operator=(const TempDir &) = delete;

    /// @brief 获取临时目录路径。
    /// @return 临时目录路径的常量引用。
    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

/// @brief 在常见安装位置中解析二进制；找不到时原样返回名称。
/// @param name 可执行文件名称。
/// @return 找到的绝对路径；找不到时返回传入的名称。
[[nodiscard]] inline std::string binary(const std::string &name) {
    for (const char *base : {"/usr/bin/", "/bin/", "/usr/local/bin/"}) {
        const std::filesystem::path candidate = std::string(base) + name;
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    return name;
}

} // namespace lsw::test
