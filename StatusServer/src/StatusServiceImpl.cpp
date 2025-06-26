#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"
#include "const.h"
#include <climits>
#include <iostream>

StatusServiceImpl::StatusServiceImpl()
{
    auto &cfg = ConfigMgr::Inst();
    auto server_list = cfg["chatservers"]["Name"];

    std::vector<std::string> words;

    std::stringstream ss(server_list);
    std::string word;

    while (std::getline(ss, word, ','))
    {
        words.push_back(word);
    }

    for (auto &word : words)
    {
        if (cfg[word]["Name"].empty())
        {
            continue;
        }

        ChatServer server;
        server.port = cfg[word]["Port"];
        server.host = cfg[word]["Host"];
        server.name = cfg[word]["Name"];

        spdlog::info("StatusServer 维护：{} {} {}", server.name, server.host, server.port);

        _servers[server.name] = server;
    }
}

std::string generate_unique_string()
{
    // 生成UUID字符串
    boost::uuids::uuid uuid = boost::uuids::random_generator()();

    // 将UUID转为字符串
    std::string unique_string = to_string(uuid);

    return unique_string;
}

Status StatusServiceImpl::GetChatServer(ServerContext *context, const GetChatServerReq *request, GetChatServerRsp *reply)
{
    std::string prefix("接收到GateServer的RPC请求 :  ");
    const auto &server = getChatServer();

    reply->set_host(server.host);
    reply->set_port(server.port);
    reply->set_error(ErrorCodes::Success);
    reply->set_token(generate_unique_string());
    insertToken(request->uid(), reply->token());
    
    return Status::OK;
}

ChatServer StatusServiceImpl::getChatServer()
{
    std::lock_guard<std::mutex> guard(_server_mtx);

    // 如果没有可用服务器，返回默认值
    if (_servers.empty())
    {
        return ChatServer();
    }

    auto minServer = _servers.begin()->second;

    // 获取第一个服务器的连接数
    auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name);
    if (count_str.empty())
    {
        // 如果为空，假设连接数为0（新启动的服务器或无连接）
        minServer.con_count = 0;
    }
    else
    {
        minServer.con_count = std::stoi(count_str);
    }

    // 使用范围for循环遍历所有服务器，找到连接数最少的
    for (auto &server : _servers)
    {
        
        // 获取当前服务器的连接数
        auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name);
        if (count_str.empty())
        {
            server.second.con_count = 0;
        }
        else
        {
            server.second.con_count = std::stoi(count_str);
        }

        // 如果当前服务器连接数更少，则选择它
        if (server.second.con_count < minServer.con_count)
        {
            minServer = server.second;
        }
    }

    spdlog::info("返回 [{}] 连接信息:{}:{} 连接数: {}", minServer.name, minServer.host, minServer.port, minServer.con_count);
    return minServer;
}

Status StatusServiceImpl::Login(ServerContext *context, const LoginReq *request, LoginRsp *reply)
{
    auto uid = request->uid();
    auto token = request->token();

    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    std::string token_value = "";
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
    if (success)
    {
        reply->set_error(ErrorCodes::UidInvalid);
        return Status::OK;
    }

    if (token_value != token)
    {
        reply->set_error(ErrorCodes::TokenInvalid);
        return Status::OK;
    }
    reply->set_error(ErrorCodes::Success);
    reply->set_uid(uid);
    reply->set_token(token);
    return Status::OK;
}

void StatusServiceImpl::insertToken(int uid, std::string token)
{
    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    RedisMgr::GetInstance()->Set(token_key, token);
}
