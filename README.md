# 实时聊天系统 (Real-time Chat System)

一个基于C++和Node.js的分布式实时聊天系统，采用微服务架构设计，支持用户注册、登录、好友管理和实时消息传递。

## 🏗️ 系统架构

本项目采用微服务架构，包含以下核心服务：

### 服务组件

- **GateServer** - 网关服务器
  - 处理HTTP请求
  - 用户认证和授权
  - 请求路由和负载均衡

- **ChatServer/ChatServer2** - 聊天服务器集群
  - 处理实时消息传递
  - WebSocket连接管理
  - 消息路由和分发
  - 支持集群部署，实现高可用

- **StatusServer** - 状态服务器
  - 用户在线状态管理
  - 服务器状态监控
  - 负载均衡支持

- **VarifyServer** - 验证服务器 (Node.js)
  - 邮箱验证码发送
  - 用户注册验证
  - 邮件服务集成

## 🛠️ 技术栈

### 后端 (C++)
- **框架**: Boost.Asio (异步网络编程)
- **通信**: gRPC (服务间通信)
- **数据库**: MySQL (用户数据存储)
- **缓存**: Redis (会话管理、状态缓存)
- **构建**: CMake + vcpkg
- **JSON处理**: JsonCpp
- **网络**: Boost.Beast (HTTP/WebSocket)

### 验证服务 (Node.js)
- **运行时**: Node.js
- **通信**: @grpc/grpc-js
- **邮件**: Nodemailer
- **缓存**: ioredis
- **工具**: UUID生成

## 📋 主要功能

### 用户管理
- ✅ 用户注册（邮箱验证）
- ✅ 用户登录/登出
- ✅ 密码修改
- ✅ 用户信息管理

### 好友系统
- ✅ 好友搜索
- ✅ 好友添加申请
- ✅ 好友申请审核
- ✅ 好友列表管理

### 实时通信
- ✅ 实时文本消息
- ✅ 消息状态通知
- ✅ 在线状态显示
- ✅ 心跳检测
- ✅ 离线消息通知

### 系统特性
- ✅ 分布式架构
- ✅ 负载均衡
- ✅ 高可用性
- ✅ 水平扩展
- ✅ 分布式锁
- ✅ 连接池管理

## 🚀 快速开始

### 环境要求

#### C++ 服务
- C++17 
- CMake 3.30.0
- vcpkg 包管理器
- MySQL 8.3.0
- Redis 3.0

#### Node.js 服务
- Node.js 16.0
- npm

### 安装依赖

#### 1. 安装 vcpkg 依赖
```bash
# 在每个C++服务目录下执行
vcpkg install --x-manifest-root=.
```

#### 2. 安装 Node.js 依赖
```bash
cd VarifyServer
npm install
```

### 配置数据库

#### MySQL 配置
1. 创建数据库和表结构
2. 更新各服务的 `config.ini` 文件中的数据库连接信息

#### Redis 配置
1. 启动 Redis 服务
2. 更新配置文件中的 Redis 连接信息

### 编译和运行

#### 1. 编译 C++ 服务
```bash
# 对每个C++服务执行
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$VCPKG_HOME/scripts/buildsystems/vcpkg.cmake ../
make
```

#### 2. 启动服务

按以下顺序启动服务：

```bash
# 1. 启动验证服务
cd VarifyServer
npm start

# 2. 启动状态服务
cd StatusServer/build
./StatusServer

# 3. 启动网关服务
cd GateServer/build
./GateServer

# 4. 启动聊天服务
cd ChatServer/build
./ChatServer

# 5. 启动聊天服务2（可选，用于集群）
cd ChatServer2/build
./ChatServer
```

## 📁 项目结构

```
ChatServer/
├── ChatServer/          # 聊天服务器1
│   ├── include/         # 头文件以及配置文件
│   ├── src/            # 源代码
│   ├── CmakeLists.txt  # CMake配置文件
│   └── vcpkg.json      # 依赖配置
├── ChatServer2/         # 聊天服务器2（集群）
├── GateServer/          # 网关服务器
├── StatusServer/        # 状态服务器
├── VarifyServer/        # 验证服务器（Node.js）
│   ├── server.js       # 主服务文件
│   ├── package.json    # Node.js依赖
│   └── config.js       # 配置文件
└── README.md           # 项目说明
```

## 🔧 配置说明

### 端口配置
- GateServer: HTTP服务端口
- ChatServer: WebSocket端口 + gRPC端口
- ChatServer2: 不同端口配置（集群部署）
- StatusServer: gRPC端口
- VarifyServer: gRPC端口

### 数据库配置
每个服务的 `config.ini` 文件包含：
- MySQL连接信息
- Redis连接信息
- 服务端口配置
- 集群节点配置

## 🔄 消息流程

1. **用户注册**: 客户端 → GateServer → VarifyServer → 邮箱验证
2. **用户登录**: 客户端 → GateServer → ChatServer → 状态更新
3. **发送消息**: 客户端 → ChatServer → 目标ChatServer → 接收方客户端
4. **状态同步**: ChatServer ↔ StatusServer ↔ Redis

## 🛡️ 安全特性

- JWT Token认证
- 密码加密存储
- 请求频率限制
- SQL注入防护
- XSS攻击防护

## 📈 性能特性

- 异步I/O处理
- 连接池管理
- Redis缓存优化
- 负载均衡
- 水平扩展支持

## 🤝 贡献指南

1. Fork 项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 打开 Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](LICENSE) 文件了解详情

## 📞 联系方式

如有问题或建议，请通过以下方式联系：
- 提交 Issue
- 发送邮件
- 创建 Pull Request

---

**注意**: 这是一个学习和演示项目，生产环境使用前请进行充分的安全性和性能测试。