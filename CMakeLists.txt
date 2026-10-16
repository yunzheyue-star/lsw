# C++26 需要 CMake >= 3.25（对齐 agent.md 基线）
cmake_minimum_required(VERSION 3.25)
project(lsw VERSION 1.0.0 LANGUAGES C CXX)

# 统一标准：C++26（对齐 PROMPT.md）
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 可选覆盖率构建：-DLSW_ENABLE_COVERAGE=ON
option(LSW_ENABLE_COVERAGE "Build with gcov coverage flags" OFF)
if(LSW_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    else()
        message(FATAL_ERROR "LSW_ENABLE_COVERAGE 仅支持 GCC/Clang")
    endif()
endif()

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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fs/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shell/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/exec/*.cpp"
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
target_compile_definitions(lsw PRIVATE LSW_VERSION="${PROJECT_VERSION}")

# ===== lsw_daemon：守护进程服务库（供 lswd 与测试复用）=====
file(GLOB_RECURSE LSW_DAEMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/daemon/*.cpp"
)
list(FILTER LSW_DAEMON_SOURCES EXCLUDE REGEX "/lswd\\.cpp$")
if(NOT LSW_DAEMON_SOURCES)
    message(FATAL_ERROR "src/daemon 下没有服务实现源文件")
endif()

add_library(lsw_daemon STATIC ${LSW_DAEMON_SOURCES})
target_link_libraries(lsw_daemon PUBLIC lsw_core)

# ===== lswd 守护进程入口 =====
add_executable(lswd "${CMAKE_CURRENT_SOURCE_DIR}/src/daemon/lswd.cpp")
target_link_libraries(lswd PRIVATE lsw_daemon)
target_compile_definitions(lswd PRIVATE LSW_VERSION="${PROJECT_VERSION}")

# ===== 测试 =====
include(CTest)          # 定义 BUILD_TESTING，默认 ON
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ===== 安装 / 卸载 / 源码打包 =====
include(GNUInstallDirs)

# helper 与数据文件安装路径；编译进 lsw_core，运行时按同一路径查找。
set(LSW_INSTALL_HELPER_PATH
    "${CMAKE_INSTALL_FULL_LIBDIR}/lsw/lsw-mount-helper"
    CACHE INTERNAL "lsw-mount-helper installed path")
set(LSW_INSTALL_DATA_DIR
    "${CMAKE_INSTALL_FULL_DATADIR}/lsw"
    CACHE INTERNAL "LSW shared data install dir")

target_compile_definitions(lsw_core PUBLIC
    LSW_INSTALL_HELPER_PATH="${LSW_INSTALL_HELPER_PATH}"
    LSW_INSTALL_DATA_DIR="${LSW_INSTALL_DATA_DIR}")

install(TARGETS lsw lswd
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/lsw-mount-helper"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/lsw")

install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/post-install.ps1"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/lsw")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs"
    FILES_MATCHING PATTERN "*.md" PATTERN "*.puml")

# make uninstall：读取 install_manifest.txt，删除安装文件与空目录。
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
    @ONLY)
add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
    COMMENT "Uninstalling LSW")

# CPack：make package_source 生成 lsw-<version>.tar.xz 源码包。
set(CPACK_PACKAGE_NAME "lsw")
set(CPACK_PACKAGE_VENDOR "LSW")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Linux Subsystem for Windows")
set(CPACK_PACKAGE_CONTACT "琳筱筱 <341178706@qq.com>")
set(CPACK_SOURCE_GENERATOR "TXZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "lsw-${PROJECT_VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
    "/build[^/]*/"
    "/.git/"
    "/.dsh/"
    "/persona/"
    "/Testing[^/]*/"
    "/.cache/"
    "\\.swp$"
    "~$")
include(CPack)
