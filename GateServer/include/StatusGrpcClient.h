#pragma once
#include "ConfigMgr.h"
#include "Singleton.h"
#include "const.h"
#include "grpc_macros.h"
#include "message.grpc.pb.h"
#include "message.pb.h"
#include <condition_variable>
#include <iostream>
#include <memory>
#include <queue>
#include <string>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;
using message::StatusService;

// StatusServer GRPC客户端连接池
class StatusConPool
{
public:
    StatusConPool(size_t poolSize, std::string host, std::string port)
        : poolSize_(poolSize),
          host_(host),
          port_(port),
          b_stop_(false)
    {
        std::string target = host + ":" + port;
        spdlog::info("StatusServer GRPC客户端连接池 创建中，连接池大小: {} 连接目的地: {}", poolSize, target);

        // 创建gRPC channel参数，强制使用指定地址
        grpc::ChannelArguments args;
        args.SetString(GRPC_ARG_DEFAULT_AUTHORITY, target);
        args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);

        for (size_t i = 0; i < poolSize_; ++i)
        {
            std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(target, grpc::InsecureChannelCredentials(), args);

            spdlog::info("StatusServer GRPC客户端连接池 {} 创建成功: {}", (i + 1), target);
            
            connections_.push(StatusService::NewStub(channel));
        }
        spdlog::info("StatusServer GRPC客户端连接池 创建结束 {} 个连接", poolSize);
    }

    ~StatusConPool()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Close();
        while (!connections_.empty())
        {
            connections_.pop();
        }
    }

    //GRPC连接池取出连接还回连接都涉及到线程同步：同步方式是锁和条件变量
    std::unique_ptr<StatusService::Stub> getConnection()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]
                   {
			if (b_stop_) {
				return true;
			}
			return !connections_.empty(); });
        // 如果停止，直接返回空指针
        if (b_stop_)
        {
            return nullptr;
        }
        auto context = std::move(connections_.front());
        connections_.pop();
        return context;
    }

    void returnConnection(std::unique_ptr<StatusService::Stub> context)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_)
        {
            return;
        }
        connections_.push(std::move(context));
        cond_.notify_one();
    }

    void Close()
    {
        b_stop_ = true;
        cond_.notify_all();
    }

private:
    atomic<bool> b_stop_;
    size_t poolSize_;
    std::string host_;
    std::string port_;
    std::queue<std::unique_ptr<StatusService::Stub>> connections_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

// StatusServer GPRC请求客户端
class StatusGrpcClient : public Singleton<StatusGrpcClient>
{
    friend class Singleton<StatusGrpcClient>;

public:
    ~StatusGrpcClient()
    {
    }
    GetChatServerRsp GetChatServer(int uid);
    LoginRsp Login(int uid, std::string token);

private:
    StatusGrpcClient();
    std::unique_ptr<StatusConPool> pool_;
};
