# CONFIGURE_DEPENDS 需要 CMake 3.12+
cmake_minimum_required(VERSION 3.12)
project(lsw VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 自动收集 src 目录（含子目录）下的所有 C++ 源文件。
# 新增/删除 .cpp 后无需手动修改本文件，构建时 CMake 会自动重新扫描。
file(GLOB_RECURSE LSW_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)

if(NOT LSW_SOURCES)
    message(FATAL_ERROR "在 ${CMAKE_CURRENT_SOURCE_DIR}/src 下没有找到任何 C++ 源文件")
endif()

add_executable(lsw ${LSW_SOURCES})

# 头文件目录：如果存在就加入搜索路径，并加入 IDE 源文件列表
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_include_directories(lsw PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    file(GLOB_RECURSE LSW_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    )
    target_sources(lsw PRIVATE ${LSW_HEADERS})
endif()

target_include_directories(lsw PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

