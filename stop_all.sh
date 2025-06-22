#!/bin/bash

# Flux Messenger 项目停止脚本
# 用于停止所有服务组件

echo "=== Flux Messenger 停止脚本 ==="
echo "正在停止所有服务..."

# 停止C++服务（main.out进程）
echo "\n--- 停止 C++ 服务 ---"
cpp_pids=$(pgrep -f "main.out")
if [ -n "$cpp_pids" ]; then
    echo "找到以下 C++ 服务进程:"
    ps aux | grep "main.out" | grep -v grep
    echo "\n正在停止 C++ 服务..."
    pkill -f "main.out"
    sleep 2
    
    # 检查是否还有残留进程
    remaining_cpp=$(pgrep -f "main.out")
    if [ -n "$remaining_cpp" ]; then
        echo "强制停止残留的 C++ 服务进程..."
        pkill -9 -f "main.out"
    fi
    echo "C++ 服务已停止"
else
    echo "没有找到运行中的 C++ 服务"
fi

# 停止Node.js服务
echo "\n--- 停止 VarifyServer (Node.js) ---"
node_pids=$(pgrep -f "VarifyServer")
if [ -n "$node_pids" ]; then
    echo "找到以下 Node.js 服务进程:"
    ps aux | grep "VarifyServer" | grep -v grep
    echo "\n正在停止 VarifyServer..."
    pkill -f "VarifyServer"
    sleep 2
    
    # 检查是否还有残留进程
    remaining_node=$(pgrep -f "VarifyServer")
    if [ -n "$remaining_node" ]; then
        echo "强制停止残留的 VarifyServer 进程..."
        pkill -9 -f "VarifyServer"
    fi
    echo "VarifyServer 已停止"
else
    echo "没有找到运行中的 VarifyServer"
fi

# 额外检查npm serve进程
echo "\n--- 检查其他相关进程 ---"
npm_pids=$(pgrep -f "npm.*serve")
if [ -n "$npm_pids" ]; then
    echo "找到 npm serve 进程，正在停止..."
    pkill -f "npm.*serve"
    sleep 1
fi

echo "\n=== 所有服务停止完成 ==="
echo "\n验证服务状态:"
echo "C++ 服务进程:"
remaining_cpp=$(pgrep -f "main.out")
if [ -n "$remaining_cpp" ]; then
    echo "警告: 仍有 C++ 服务在运行"
    ps aux | grep "main.out" | grep -v grep
else
    echo "✓ 所有 C++ 服务已停止"
fi

echo "\nNode.js 服务进程:"
remaining_node=$(pgrep -f "VarifyServer\|npm.*serve")
if [ -n "$remaining_node" ]; then
    echo "警告: 仍有 Node.js 服务在运行"
    ps aux | grep -E "VarifyServer|npm.*serve" | grep -v grep
else
    echo "✓ 所有 Node.js 服务已停止"
fi