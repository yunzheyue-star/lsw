# C++26 需要 CMake >= 3.25（对齐 agent.md 基线）
cmake_minimum_required(VERSION 3.25)
project(lsw VERSION 0.1.0 LANGUAGES C CXX)

# 统一标准：C++26（对齐 PROMPT.md）
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===== 第三方依赖 =====
# 共享依赖源码缓存：不同构建目录（含验收脚本的临时构建）复用同一份，避免重复下载
if(NOT DEFINED FETCHCONTENT_BASE_DIR)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/build/_deps" CACHE PATH "FetchContent 共享缓存目录")
endif()
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "已有依赖源码不再联网更新" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

# ===== lsw_core：核心静态库（src/core + src/util）=====
# 按模块 glob 并 CONFIGURE_DEPENDS：模块目录内新增 .cpp 无需修改本文件。
file(GLOB_RECURSE LSW_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/util/*.cpp"
)
if(NOT LSW_CORE_SOURCES)
    message(FATAL_ERROR "src/core 与 src/util 下没有找到任何 C++ 源文件")
endif()

add_library(lsw_core STATIC ${LSW_CORE_SOURCES})
target_include_directories(lsw_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(lsw_core PUBLIC lsw_deps)

# 头文件加入目标，便于 IDE 索引（PURE 内容不参与编译）
file(GLOB_RECURSE LSW_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)
if(LSW_HEADERS)
    target_sources(lsw_core PRIVATE ${LSW_HEADERS})
endif()

# ===== lsw CLI 可执行文件（src/cli）=====
file(GLOB_RECURSE LSW_CLI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.cpp"
)
if(NOT LSW_CLI_SOURCES)
    message(FATAL_ERROR "src/cli 下没有找到任何 C++ 源文件")
endif()

add_executable(lsw ${LSW_CLI_SOURCES})
target_link_libraries(lsw PRIVATE lsw_core)

# ===== 测试 =====
include(CTest)          # 定义 BUILD_TESTING，默认 ON
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
