# ============================================================
# 第三方依赖解析：优先系统包，缺失时 FetchContent 下载
#
# 选项：
#   -DLSW_FETCH_DEPS=OFF                     只用系统包（离线/打包环境）
#   -DFETCHCONTENT_SOURCE_DIR_<NAME>=<path>  指定本地依赖源码目录
#
# 依赖：CLI11、nlohmann/json、spdlog、fmt、toml++；Catch2 仅供测试。
# ============================================================
include(FetchContent)

option(LSW_FETCH_DEPS "系统缺少依赖时通过 FetchContent 下载" ON)

set(LSW_MISSING_DEPS "")

function(lsw_missing_dep name)
    set(LSW_MISSING_DEPS "${LSW_MISSING_DEPS} ${name}" PARENT_SCOPE)
endfunction()

# ---------- nlohmann/json ----------
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    if(LSW_FETCH_DEPS)
        set(JSON_BuildTests OFF CACHE INTERNAL "")
        FetchContent_Declare(nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(nlohmann_json)
    else()
        lsw_missing_dep(nlohmann_json)
    endif()
endif()

# ---------- fmt ----------
find_package(fmt CONFIG QUIET)
if(NOT fmt_FOUND)
    if(LSW_FETCH_DEPS)
        set(FMT_TEST OFF CACHE INTERNAL "")
        set(FMT_INSTALL OFF CACHE INTERNAL "")
        FetchContent_Declare(fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG 11.2.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(fmt)
    else()
        lsw_missing_dep(fmt)
    endif()
endif()

# ---------- spdlog ----------
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    if(LSW_FETCH_DEPS)
        set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
        set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
        # 使用自带 fmt，避免与系统 fmt 版本混用
        set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.15.3
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(spdlog)
    else()
        lsw_missing_dep(spdlog)
    endif()
endif()

# ---------- CLI11 ----------
find_package(CLI11 CONFIG QUIET)
if(NOT CLI11_FOUND)
    if(LSW_FETCH_DEPS)
        set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(CLI11
            GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
            GIT_TAG v2.7.2
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(CLI11)
    else()
        lsw_missing_dep(CLI11)
    endif()
endif()

# ---------- toml++ ----------
find_package(tomlplusplus CONFIG QUIET)
if(NOT tomlplusplus_FOUND)
    if(LSW_FETCH_DEPS)
        set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(tomlplusplus
            GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
            GIT_TAG v3.4.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(tomlplusplus)
    else()
        lsw_missing_dep(tomlplusplus)
    endif()
endif()

if(LSW_MISSING_DEPS)
    message(FATAL_ERROR
        "缺少依赖：${LSW_MISSING_DEPS}\n"
        "请安装对应系统包，或保持 LSW_FETCH_DEPS=ON（默认）以自动下载。")
endif()

# 聚合第三方依赖，供 lsw_core 使用
add_library(lsw_deps INTERFACE)
target_link_libraries(lsw_deps INTERFACE
    CLI11::CLI11
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    fmt::fmt
    tomlplusplus::tomlplusplus
)

# ---------- Catch2（仅测试使用，按需解析） ----------
function(lsw_resolve_catch2)
    find_package(Catch2 3 CONFIG QUIET)
    if(NOT Catch2_FOUND)
        if(LSW_FETCH_DEPS)
            set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
            set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
            set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(Catch2
                GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                GIT_TAG v3.8.1
                GIT_SHALLOW TRUE)
            FetchContent_MakeAvailable(Catch2)
            # 让 include(Catch)/catch_discover_tests 可用
            list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
            set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
        else()
            message(FATAL_ERROR "缺少依赖 Catch2（测试需要）；安装系统包或开启 LSW_FETCH_DEPS。")
        endif()
    else()
        # 系统 Catch2：Catch.cmake 位于 ${Catch2_DIR}；find_package 在
        # function 作用域内修改的 CMAKE_MODULE_PATH 需要显式传回调用方，
        # 否则 tests/CMakeLists.txt 的 include(Catch) 会失败。
        list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
        set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
    endif()
endfunction()
