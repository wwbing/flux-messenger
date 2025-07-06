cmake_minimum_required(VERSION 3.30)
project(StatusServer)

message(STATUS "========== 开始配置 ${PROJECT_NAME} 项目 ==========")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # 生成 compile_commands.json 供 clangd 使用

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# 设置头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)


# 使用 file 命令自动查找所有 C++ 源文件
file(GLOB SRC 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# 添加 Protobuf 和 gRPC生成的源文件
set(PROTO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/message.pb.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/message.grpc.pb.cc
)

# 查找 fmt 包
find_package(fmt CONFIG REQUIRED)

# 查找 Boost 包 (使用现代 CMake 方式，兼容 vcpkg)
find_package(Boost REQUIRED COMPONENTS system filesystem)

# 查找 JsonCpp 包
find_package(jsoncpp CONFIG REQUIRED)

# 查找 gRPC 包
find_package(gRPC CONFIG REQUIRED)

# 查找 MySQL Connector C++ 包
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)

# 查找 hiredis 包
find_package(hiredis CONFIG REQUIRED)

# 查找 spdlog 包
find_package(spdlog CONFIG REQUIRED)

# 添加可执行文件
add_executable(main.out ${SRC} ${PROTO_SOURCES})


# 链接所需库
target_link_libraries(main.out 
    fmt::fmt
    Boost::system
    Boost::filesystem
    JsonCpp::JsonCpp
    gRPC::grpc++
    gRPC::grpc
    unofficial::mysql-connector-cpp::connector
    resolv  # 添加DNS解析库
    dl      # 添加动态链接库
    pthread # 添加线程库
    hiredis::hiredis
    spdlog::spdlog
)
