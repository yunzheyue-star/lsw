/**
 * @file test_path_map.cpp
 * @brief test_path_map 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_path_map.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <lsw/core/path_map.hpp>

#include "test_support.hpp"

using lsw::ErrorCode;
using lsw::is_within_home;
using lsw::linux_to_z;
using lsw::map_cwd_to_z;

TEST_CASE("HOME 本身映射为默认路径", "[core][path_map]") {
    const auto mapped = map_cwd_to_z("/home/alice", "/home/alice");
    REQUIRE(mapped.has_value());
    CHECK(*mapped == "");
    CHECK_FALSE(linux_to_z("/home/alice", "/home/alice").has_value());
}

TEST_CASE("HOME 之下映射为 Z: 路径", "[core][path_map]") {
    const lsw::test::TempDir home("home-inside");
    const auto project = home.path() / "Project" / "demo";
    std::filesystem::create_directories(project);

    const auto mapped = map_cwd_to_z(project, home.path());
    REQUIRE(mapped.has_value());
    CHECK(*mapped == "Z:\\Project\\demo");

    const auto short_form = linux_to_z(project, home.path());
    REQUIRE(short_form.has_value());
    CHECK(*short_form == "Z:\\Project\\demo");
    CHECK(is_within_home(project, home.path()));
}

TEST_CASE("空格与中文路径原样保留", "[core][path_map]") {
    const lsw::test::TempDir home("home-unicode");
    const auto project = home.path() / "我的 项目" / "子目录";
    std::filesystem::create_directories(project);

    const auto mapped = map_cwd_to_z(project, home.path());
    REQUIRE(mapped.has_value());
    CHECK(*mapped == "Z:\\我的 项目\\子目录");
}

TEST_CASE("HOME 之外映射失败", "[core][path_map]") {
    const auto mapped = map_cwd_to_z("/tmp/elsewhere", "/home/alice");
    REQUIRE_FALSE(mapped.has_value());
    CHECK(mapped.error().code == ErrorCode::kPathMappingFailed);
    CHECK_FALSE(linux_to_z("/tmp/elsewhere", "/home/alice").has_value());
    CHECK_FALSE(is_within_home("/tmp/elsewhere", "/home/alice"));
}

TEST_CASE("前缀相似的目录不算 HOME 内", "[core][path_map]") {
    const lsw::test::TempDir base("home-prefix");
    const auto home = base.path() / "u";
    const auto lookalike = base.path() / "u2";
    std::filesystem::create_directories(home);
    std::filesystem::create_directories(lookalike);

    CHECK_FALSE(is_within_home(lookalike, home));
    const auto mapped = map_cwd_to_z(lookalike, home);
    REQUIRE_FALSE(mapped.has_value());
    CHECK(mapped.error().code == ErrorCode::kPathMappingFailed);
}

TEST_CASE(".. 逃逸 HOME 被拒绝", "[core][path_map]") {
    const lsw::test::TempDir base("home-escape");
    const auto home = base.path() / "home";
    const auto escape = home / ".." / "escape";
    std::filesystem::create_directories(home);
    std::filesystem::create_directories(base.path() / "escape");

    CHECK_FALSE(is_within_home(escape, home));
    CHECK_FALSE(map_cwd_to_z(escape, home).has_value());
}
