﻿// ChatServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "LogicSystem.h"
#include <csignal>
#include <ostream>
#include <thread>
#include <mutex>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"
#include "ChatServiceImpl.h"
#include "const.h"

using namespace std;
bool bstop = false;
std::condition_variable cond_quit;
std::mutex mutex_quit;
/*
    假设来了64个客户端的连接请求，此时Chatserver2服务器：
        ioc线程池32个ioc线程，每个都对应2个客户端的连接。
        Cserver维护了一个Cession的连接池，目前一共有64个连接。

    然后这64个连接里面，每两个连接由一个ioc线程管理，IO多路复用隐藏在boost::asio内部 ，通过epoll实现
	这就是经典的 Reactor 处理的模式

*/
int main()
{	
	auto& cfg = ConfigMgr::Inst();
	auto server_name = cfg["SelfServer"]["Name"];
	try {
		auto pool = AsioIOServicePool::GetInstance();
        // 将登录数设置为0
        spdlog::info("ChatServer2 初始化设置登陆数为 0");
		RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, "0");

        Defer derfer([server_name]()
            {
				RedisMgr::GetInstance()->HDel(LOGIN_COUNT, server_name);
				RedisMgr::GetInstance()->Close();
			}
		);

        spdlog::info("ChatServer2 : cfg SelfServer Port {}", cfg["SelfServer"]["Port"]);
        auto port_str = cfg["SelfServer"]["Port"];

        boost::asio::io_context io_context;
		//创建Cserver智能指针------>这里开启了监听客户端主动的tcp连接请求
		auto pointer_server = std::make_shared<CServer>(io_context, atoi(port_str.c_str()));
		//启动定时器
		pointer_server->StartTimer();

		//定义一个GrpcServer
		std::string server_address(cfg["SelfServer"]["Host"] + ":" + cfg["SelfServer"]["RPCPort"]);
		ChatServiceImpl service;
		grpc::ServerBuilder builder;
		// 监听端口和添加服务
		builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
		builder.RegisterService(&service);
		service.RegisterServer(pointer_server);
		// 构建并启动gRPC服务器
		std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
		// 打印服务器地址
		spdlog::info("ChatServer2 RPC 正在监听端口：{}", server_address);

		//单独启动一个线程处理grpc服务
		std::thread  grpc_server_thread([&server]() {
				server->Wait();
			});

	
		boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
		signals.async_wait([&io_context, pool, &server](auto, auto) {
			io_context.stop();
			pool->Stop();
			server->Shutdown();
			});
		
	
		//将Cserver注册给逻辑类方便以后清除连接
		LogicSystem::GetInstance()->SetServer(pointer_server);
		io_context.run();

		grpc_server_thread.join();
		pointer_server->StopTimer();
		return 0;
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << endl;
	}

}

