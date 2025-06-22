#!/bin/bash
# 切换到项目目录
# cd ~/Code/Project/Real-Time-Chat-Server/GateServer

# 确保输出目录存在
mkdir -p ./src ./include

# 使用 vcpkg 安装的工具
VCPKG_INSTALLED_PATH="./vcpkg_installed/x64-linux"
PROTOC_PATH="$VCPKG_INSTALLED_PATH/tools/protobuf/protoc"
PLUGIN_PATH="$VCPKG_INSTALLED_PATH/tools/grpc/grpc_cpp_plugin"

# 检查 protoc 是否存在
if [ ! -f "$PROTOC_PATH" ]; then
    echo "错误: 找不到 vcpkg 安装的 protoc: $PROTOC_PATH"
    echo "尝试使用系统 protoc..."
    PROTOC_PATH="protoc"
fi

# 检查 grpc_cpp_plugin 是否存在
if [ ! -f "$PLUGIN_PATH" ]; then
    echo "错误: 找不到 vcpkg 安装的 grpc_cpp_plugin: $PLUGIN_PATH"
    echo "尝试查找系统安装的 grpc_cpp_plugin..."
    PLUGIN_PATH=$(find /usr -name "grpc_cpp_plugin" -type f 2>/dev/null | head -n 1)
    if [ -z "$PLUGIN_PATH" ]; then
        echo "错误: 找不到 grpc_cpp_plugin。请确保已安装 gRPC C++ 插件。"
        exit 1
    fi
fi

echo "使用 protoc: $PROTOC_PATH"
echo "使用 gRPC C++ 插件: $PLUGIN_PATH"

# 生成 Protocol Buffers 文件(.pb.h 到 include, .pb.cc 到 src)
echo "正在生成 Protocol Buffers 文件..."
$PROTOC_PATH -I=./include --cpp_out=./include ./include/message.proto
mv ./include/message.pb.cc ./src/

if [ $? -ne 0 ]; then
    echo "错误: 生成 Protocol Buffers 文件失败"
    exit 1
fi

# 生成 gRPC 服务文件(.grpc.pb.h 到 include, .grpc.pb.cc 到 src)
echo "正在生成 gRPC 服务文件..."
$PROTOC_PATH -I=./include --grpc_out=./include --plugin=protoc-gen-grpc=$PLUGIN_PATH ./include/message.proto
mv ./include/message.grpc.pb.cc ./src/

if [ $? -ne 0 ]; then
    echo "错误: 生成 gRPC 服务文件失败"
    exit 1
fi

echo "文件生成完成。检查以下文件是否存在:"
ls -la ./src/message.pb.cc ./src/message.grpc.pb.cc ./include/message.pb.h ./include/message.grpc.pb.h

echo "成功完成!"
