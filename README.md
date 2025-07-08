# Flux Messenger (流动信使)

Flux Messenger 是一个基于C++和微服务架构的分布式实时聊天应用。项目旨在实践高并发后端技术，实现了一个包含用户服务、好友关系、实时通信和离线消息等功能的核心聊天系统。

## ✨ 主要功能

*   **用户服务:** 支持注册（邮件验证）、登录及资料管理。
*   **好友系统:** 支持用户搜索、好友请求及列表管理。
*   **实时通信:** 基于WebSocket的即时消息及在线状态同步。
*   **离线消息:** 保证用户离线期间的消息可达性。
*   **水平扩展:** 微服务架构设计，支持各服务独立部署与扩展。

## 🛠️ 技术架构

系统采用微服务架构，将不同业务逻辑拆分为独立的服务，服务间通过gRPC进行高效通信。

### 核心服务

*   **`GateServer` (C++)**: API网关，负责客户端HTTP请求的接入、用户认证及流量路由。
*   **`ChatServer` (C++)**: 聊天核心服务，管理WebSocket长连接，处理实时消息路由。支持多实例部署以分担负载。
*   **`StatusServer` (C++)**: 状态管理服务，通过Redis维护用户的在线状态及所在`ChatServer`的映射关系，实现跨服务的消息投递。
*   **`VarifyServer` (Node.js)**: 独立的验证服务，负责处理注册邮件的发送。

### 技术栈

*   **后端:** C++17, Boost.Asio (异步网络)
*   **服务间通信:** gRPC, Protocol Buffers
*   **数据存储:** MySQL (持久化), Redis (状态缓存与分布式锁)
*   **构建与依赖:** CMake, vcpkg
*   **部署:** Docker

## 🚀 快速开始

### 环境依赖

*   C++17 编译器, CMake (>= 3.30), vcpkg
*   Docker, Docker Compose
*   Node.js (>= 16.0), npm

### 1. 启动基础服务

项目使用Docker Compose来管理MySQL和Redis，简化了环境搭建。

```bash
# 在项目根目录执行 todo:还未实现dockerfile
docker-compose up -d
```

### 2. 安装与配置

```bash
# 安装C++服务依赖
# (在GateServer, ChatServer, StatusServer目录下分别执行)
vcpkg install --x-manifest-root=.

# 安装Node.js服务依赖
cd VarifyServer && npm install
```

接下来，请根据 `config.ini.example` 和 `config.js.example` 创建并修改每个服务的配置文件，填入正确的数据库和Redis连接信息。

### 3. 构建与运行

请按照以下顺序编译并启动服务：

```bash
# 1. 编译所有C++服务
# (在GateServer, ChatServer, StatusServer目录下分别执行)
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$VCPKG_HOME/scripts/buildsystems/vcpkg.cmake ..
make -j$(nproc)

# 2. 依次启动所有服务 (请在各自的目录下执行)
./VarifyServer/server.js
./StatusServer/build/main.out
./GateServer/build/main.out
./ChatServer/build/main.out
# ./ChatServer2/build/main.out # (可选)
```

服务启动后，系统即可正常运行。


