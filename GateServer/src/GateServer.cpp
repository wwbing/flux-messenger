// GateServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "const.h"
#include <hiredis/hiredis.h>
#include <iostream>
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>


int main()
{
    try
    {
        MysqlMgr::GetInstance();
        RedisMgr::GetInstance();

        auto &gCfgMgr = ConfigMgr::Inst();
        std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
        unsigned short gate_port = atoi(gate_port_str.c_str());

        //这个ioc IO操作管理器 ：1：管理异常信号 2：管理CServer对客户端连接的监听IO
        net::io_context ioc{1};

        //1：管理异常信号
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait(
            [&ioc](const boost::system::error_code &error, int signal_number)
            {
                if (error)
                {
                    return;
                }
                ioc.stop();
            });

        //2：管理CServer对客户端连接的监听IO
        std::make_shared<CServer>(ioc, gate_port)->Start();
        spdlog::info("GateServer 监听端口: {}", gate_port);
        ioc.run();
        RedisMgr::GetInstance()->Close();
    }
    catch (std::exception const &e)
    {
        spdlog::error("异常: {}", e.what());
        RedisMgr::GetInstance()->Close();
        return EXIT_FAILURE;
    }
}
