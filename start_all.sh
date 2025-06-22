#!/bin/bash

# Flux Messenger 项目启动脚本
# 用于启动所有服务组件

echo "=== Flux Messenger 启动脚本 ==="
echo "正在启动所有服务..."

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "项目根目录: $PROJECT_ROOT"

# 检查build目录是否存在的函数
check_build_dir() {
    local service_name=$1
    local build_path="$PROJECT_ROOT/$service_name/build"
    
    if [ ! -d "$build_path" ]; then
        echo "错误: $service_name/build 目录不存在，请先编译项目"
        return 1
    fi
    
    if [ ! -f "$build_path/main.out" ]; then
        echo "错误: $service_name/build/main.out 不存在，请先编译项目"
        return 1
    fi
    
    return 0
}

# 启动C++服务的函数
start_cpp_service() {
    local service_name=$1
    echo "\n--- 启动 $service_name ---"
    
    if check_build_dir "$service_name"; then
        cd "$PROJECT_ROOT/$service_name/build"
        echo "在目录 $(pwd) 中启动 $service_name"
        ./main.out &
        local pid=$!
        echo "$service_name 已启动，PID: $pid"
        cd "$PROJECT_ROOT"
    else
        echo "跳过 $service_name 启动"
    fi
}

# 启动Node.js服务的函数
start_node_service() {
    echo "\n--- 启动 VarifyServer ---"
    
    if [ ! -d "$PROJECT_ROOT/VarifyServer" ]; then
        echo "错误: VarifyServer 目录不存在"
        return 1
    fi
    
    if [ ! -f "$PROJECT_ROOT/VarifyServer/package.json" ]; then
        echo "错误: VarifyServer/package.json 不存在"
        return 1
    fi
    
    cd "$PROJECT_ROOT/VarifyServer"
    echo "在目录 $(pwd) 中启动 VarifyServer"
    
    # 检查是否安装了依赖
    if [ ! -d "node_modules" ]; then
        echo "正在安装 npm 依赖..."
        npm install
    fi
    
    npm run serve &
    local pid=$!
    echo "VarifyServer 已启动，PID: $pid"
    cd "$PROJECT_ROOT"
}

# 启动所有服务
echo "\n开始启动各个服务组件..."

# 启动C++服务
start_cpp_service "GateServer"
start_cpp_service "ChatServer"
start_cpp_service "ChatServer2"
start_cpp_service "StatusServer"

# 启动Node.js服务
start_node_service

echo "\n=== 所有服务启动完成 ==="
echo "\n提示:"
echo "- 使用 'ps aux | grep main.out' 查看C++服务状态"
echo "- 使用 'ps aux | grep node' 查看Node.js服务状态"
echo "- 使用 './stop_all.sh' 停止所有服务（如果存在）"
echo "- 按 Ctrl+C 可以停止此脚本，但服务会继续在后台运行"

echo "\n脚本执行完成，所有服务已在后台运行"